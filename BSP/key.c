#include "key.h"
#include "main.h"

#define KEY_SCAN_PERIOD_MS      5U
#define KEY_DEBOUNCE_MS         20U
#define KEY_DEBOUNCE_COUNT      (KEY_DEBOUNCE_MS / KEY_SCAN_PERIOD_MS)
#define KEY_QUEUE_DEPTH         32U
#define KEY_THREAD_STACK_WORDS  256U

typedef struct
{
	GPIO_TypeDef *port;
	u16 pin;
	u32 mask;
} KEY_Hw_t;

static const KEY_Hw_t key_hw_table[KEY_COUNT] =
{
	{KEY1_GPIO_Port, KEY1_Pin, KEY_MASK_1},
	{KEY2_GPIO_Port, KEY2_Pin, KEY_MASK_2},
	{KEY3_GPIO_Port, KEY3_Pin, KEY_MASK_3},
	{KEY4_GPIO_Port, KEY4_Pin, KEY_MASK_4},
	{KEY5_GPIO_Port, KEY5_Pin, KEY_MASK_5},
	{KEY6_GPIO_Port, KEY6_Pin, KEY_MASK_6},
	{KEY7_GPIO_Port, KEY7_Pin, KEY_MASK_7},
	{KEY8_GPIO_Port, KEY8_Pin, KEY_MASK_8},
};

static osThreadId_t key_thread_id;
static osMessageQueueId_t key_event_queue;

static StaticTask_t key_thread_cb;
static u32 key_thread_stack[KEY_THREAD_STACK_WORDS];
static const osThreadAttr_t key_thread_attr =
{
	.name = "KEY",
	.cb_mem = &key_thread_cb,
	.cb_size = sizeof(key_thread_cb),
	.stack_mem = &key_thread_stack[0],
	.stack_size = sizeof(key_thread_stack),
	.priority = (osPriority_t)osPriorityLow7,
};

static StaticQueue_t key_queue_cb;
static u32 key_queue_mem[(KEY_QUEUE_DEPTH * sizeof(KEY_Event_t) + 3U) / 4U];
static const osMessageQueueAttr_t key_queue_attr =
{
	.name = "KEY_Event",
	.cb_mem = &key_queue_cb,
	.cb_size = sizeof(key_queue_cb),
	.mq_mem = &key_queue_mem[0],
	.mq_size = sizeof(key_queue_mem),
};

static volatile u32 key_raw_state;
static volatile u32 key_stable_state;

static u32 key_read_gpio_state(void)
{
	u32 state = 0;
	u8 i;

	for (i = 0; i < KEY_COUNT; i++)
	{
		if (HAL_GPIO_ReadPin(key_hw_table[i].port, key_hw_table[i].pin) == GPIO_PIN_RESET)
		{
			state |= key_hw_table[i].mask;
		}
	}

	return state & KEY_MASK_ALL;
}

static void key_push_event(KEY_Id_t id, KEY_EventType_t type)
{
	KEY_Event_t event;
	KEY_Event_t dropped;

	if (key_event_queue == NULL)
	{
		return;
	}

	event.id = id;
	event.type = type;
	event.mask = (1UL << id);
	event.tick = osKernelGetTickCount();

	if (osMessageQueuePut(key_event_queue, &event, 0, 0) != osOK)
	{
		(void)osMessageQueueGet(key_event_queue, &dropped, NULL, 0);
		(void)osMessageQueuePut(key_event_queue, &event, 0, 0);
	}
}

static void key_scan_task(void *argument)
{
	u8 debounce_cnt[KEY_COUNT] = {0};
	u32 stable;
	u32 raw;
	u8 i;

	(void)argument;

	raw = key_read_gpio_state();
	key_raw_state = raw;
	key_stable_state = raw;

	while (1)
	{
		raw = key_read_gpio_state();
		key_raw_state = raw;
		stable = key_stable_state;

		for (i = 0; i < KEY_COUNT; i++)
		{
			u32 mask = key_hw_table[i].mask;

			if ((raw & mask) != (stable & mask))
			{
				if (debounce_cnt[i] < KEY_DEBOUNCE_COUNT)
				{
					debounce_cnt[i]++;
				}

				if (debounce_cnt[i] >= KEY_DEBOUNCE_COUNT)
				{
					if ((raw & mask) != 0U)
					{
						stable |= mask;
						key_stable_state = stable;
						key_push_event((KEY_Id_t)i, KEY_EVENT_PRESS);
					}
					else
					{
						stable &= ~mask;
						key_stable_state = stable;
						key_push_event((KEY_Id_t)i, KEY_EVENT_RELEASE);
					}

					debounce_cnt[i] = 0;
				}
			}
			else
			{
				debounce_cnt[i] = 0;
			}
		}

		key_stable_state = stable;
		osDelay(KEY_SCAN_PERIOD_MS);
	}
}

int KEY_Init(void)
{
	if (key_thread_id != NULL)
	{
		return KEY_OK;
	}

	key_raw_state = key_read_gpio_state();
	key_stable_state = key_raw_state;

	if (key_event_queue == NULL)
	{
		key_event_queue = osMessageQueueNew(KEY_QUEUE_DEPTH, sizeof(KEY_Event_t), &key_queue_attr);
		if (key_event_queue == NULL)
		{
			return KEY_ERROR;
		}
	}

	key_thread_id = osThreadNew(key_scan_task, NULL, &key_thread_attr);
	if (key_thread_id == NULL)
	{
		return KEY_ERROR;
	}

	return KEY_OK;
}

u32 KEY_GetState(void)
{
	return key_stable_state & KEY_MASK_ALL;
}

u32 KEY_GetRawState(void)
{
	return key_read_gpio_state();
}

u8 KEY_IsPressed(KEY_Id_t id)
{
	if (id >= KEY_ID_MAX)
	{
		return 0;
	}

	return ((KEY_GetState() & (1UL << id)) != 0U) ? 1U : 0U;
}

osStatus_t KEY_GetEvent(KEY_Event_t *event, u32 timeout)
{
	if (event == NULL)
	{
		return osErrorParameter;
	}

	if (key_event_queue == NULL)
	{
		return osErrorResource;
	}

	return osMessageQueueGet(key_event_queue, event, NULL, timeout);
}

osMessageQueueId_t KEY_GetEventQueue(void)
{
	return key_event_queue;
}
