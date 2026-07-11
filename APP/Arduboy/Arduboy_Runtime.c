#include "Arduboy_Runtime.h"

#include "FreeRTOS.h"
#include "Malloc.h"
#include "System_Status.h"
#include "arduboy.h"
#include "cmsis_os2.h"

#define ARDUBOY_RUNTIME_STACK_SIZE       4096U
#define ARDUBOY_RUNTIME_START_FLAG       (1UL << 0)
#define ARDUBOY_RUNTIME_LAUNCH_DELAY_MS  80U
#define ARDUBOY_RUNTIME_NO_GAME          0xFFU

static void Arduboy_Runtime_Task(void *argument);
static int Arduboy_Runtime_EnsureWorker(void);

static StaticTask_t s_thread_cb;
static osThreadAttr_t s_thread_attr =
{
	.name = "Arduboy",
	.cb_mem = &s_thread_cb,
	.cb_size = sizeof(s_thread_cb),
	.stack_mem = NULL,
	.stack_size = ARDUBOY_RUNTIME_STACK_SIZE,
	.priority = (osPriority_t)osPriorityNormal,
};

static osThreadId_t s_thread_id;
static volatile Arduboy_RuntimeState_t s_state = ARDUBOY_RUNTIME_IDLE;
static volatile Arduboy_RuntimeResult_t s_result = ARDUBOY_RUNTIME_RESULT_NONE;
static volatile u8 s_game_index = ARDUBOY_RUNTIME_NO_GAME;
static const Arduboy_Game_t *s_selected_game;
static u8 s_core_tested;

int Arduboy_Runtime_Start(u8 game_index)
{
	const Arduboy_Game_t *game;
	u32 flags;
	int code;

	game = Arduboy_Games_Get(game_index);
	if (game == NULL)
	{
		s_result = ARDUBOY_RUNTIME_RESULT_INVALID_GAME;
		return OPERATE_FAILED;
	}

	if (Arduboy_Runtime_IsActive() != 0U)
	{
		return OPERATE_FAILED;
	}

	code = Arduboy_Runtime_EnsureWorker();
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	Arduboy_Port_ResetStop();
	s_selected_game = game;
	s_game_index = game_index;
	s_result = ARDUBOY_RUNTIME_RESULT_NONE;
	s_state = ARDUBOY_RUNTIME_STARTING;
	__DMB();
	flags = osThreadFlagsSet(s_thread_id, ARDUBOY_RUNTIME_START_FLAG);
	if ((flags & osFlagsError) != 0U)
	{
		s_result = ARDUBOY_RUNTIME_RESULT_THREAD_FAILED;
		s_state = ARDUBOY_RUNTIME_ERROR;
		return OPERATE_FAILED;
	}

	return SYSTEM_STATUS_OK;
}

void Arduboy_Runtime_RequestStop(void)
{
	if (Arduboy_Runtime_IsActive() != 0U)
	{
		s_state = ARDUBOY_RUNTIME_STOPPING;
		Arduboy_Port_RequestStop();
	}
}

Arduboy_RuntimeState_t Arduboy_Runtime_GetState(void)
{
	return s_state;
}

Arduboy_RuntimeResult_t Arduboy_Runtime_GetResult(void)
{
	return s_result;
}

u8 Arduboy_Runtime_GetGameIndex(void)
{
	return s_game_index;
}

u8 Arduboy_Runtime_IsActive(void)
{
	return (s_state == ARDUBOY_RUNTIME_STARTING ||
	        s_state == ARDUBOY_RUNTIME_RUNNING ||
	        s_state == ARDUBOY_RUNTIME_STOPPING) ? 1U : 0U;
}

u32 Arduboy_Runtime_GetStackSpace(void)
{
	return (s_thread_id != NULL) ? osThreadGetStackSpace(s_thread_id) : 0U;
}

const char *Arduboy_Runtime_ResultText(Arduboy_RuntimeResult_t result)
{
	switch (result)
	{
		case ARDUBOY_RUNTIME_RESULT_NONE:
			return "Ready";
		case ARDUBOY_RUNTIME_RESULT_EXITED:
			return "Game exited";
		case ARDUBOY_RUNTIME_RESULT_INVALID_GAME:
			return "Invalid game";
		case ARDUBOY_RUNTIME_RESULT_CORE_TEST_FAILED:
			return "Core self-test failed";
		case ARDUBOY_RUNTIME_RESULT_STACK_FAILED:
			return "CCM stack allocation failed";
		case ARDUBOY_RUNTIME_RESULT_THREAD_FAILED:
			return "Game task failed";
		case ARDUBOY_RUNTIME_RESULT_DISPLAY_BUSY:
			return "Display is busy";
		case ARDUBOY_RUNTIME_RESULT_FRAMEBUFFER_FAILED:
			return "SRAMEX framebuffer failed";
		case ARDUBOY_RUNTIME_RESULT_DISPLAY_RESUME_FAILED:
			return "LVGL resume failed";
		default:
			return "Arduboy error";
	}
}

static int Arduboy_Runtime_EnsureWorker(void)
{
	if (s_core_tested == 0U)
	{
		if (Arduboy_SelfTest() == 0U)
		{
			s_result = ARDUBOY_RUNTIME_RESULT_CORE_TEST_FAILED;
			s_state = ARDUBOY_RUNTIME_ERROR;
			return OPERATE_FAILED;
		}
		s_core_tested = 1U;
	}

	if (s_thread_id != NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	if (s_thread_attr.stack_mem == NULL)
	{
		s_thread_attr.stack_mem = sys_thread_stack_alloc((u32)s_thread_attr.stack_size);
	}
	if (s_thread_attr.stack_mem == NULL)
	{
		s_result = ARDUBOY_RUNTIME_RESULT_STACK_FAILED;
		s_state = ARDUBOY_RUNTIME_ERROR;
		return OUT_OF_HEAP_MEMORY;
	}

	s_thread_id = osThreadNew(Arduboy_Runtime_Task, NULL, &s_thread_attr);
	if (s_thread_id == NULL)
	{
		s_result = ARDUBOY_RUNTIME_RESULT_THREAD_FAILED;
		s_state = ARDUBOY_RUNTIME_ERROR;
		return OPERATE_FAILED;
	}

	return SYSTEM_STATUS_OK;
}

static void Arduboy_Runtime_Task(void *argument)
{
	const Arduboy_Game_t *game;
	u32 flags;
	int code;

	(void)argument;
	for (;;)
	{
		flags = osThreadFlagsWait(ARDUBOY_RUNTIME_START_FLAG, osFlagsWaitAny, osWaitForever);
		if ((flags & osFlagsError) != 0U)
		{
			continue;
		}

		__DMB();
		game = s_selected_game;
		if (game == NULL)
		{
			s_result = ARDUBOY_RUNTIME_RESULT_INVALID_GAME;
			s_state = ARDUBOY_RUNTIME_ERROR;
			continue;
		}

		(void)osDelay(ARDUBOY_RUNTIME_LAUNCH_DELAY_MS);
		if (Arduboy_Port_ShouldStop() != 0U)
		{
			s_result = ARDUBOY_RUNTIME_RESULT_EXITED;
			s_state = ARDUBOY_RUNTIME_FINISHED;
			continue;
		}

		code = lvgl_stop_ui();
		if (code != SYSTEM_STATUS_OK)
		{
			s_result = ARDUBOY_RUNTIME_RESULT_DISPLAY_BUSY;
			s_state = ARDUBOY_RUNTIME_ERROR;
			continue;
		}

		code = Arduboy_Port_Begin();
		if (code != SYSTEM_STATUS_OK)
		{
			s_result = ARDUBOY_RUNTIME_RESULT_FRAMEBUFFER_FAILED;
			(void)lvgl_start_ui();
			s_state = ARDUBOY_RUNTIME_ERROR;
			continue;
		}

		s_state = ARDUBOY_RUNTIME_RUNNING;
		game->init();
		while (Arduboy_Port_ShouldStop() == 0U)
		{
			game->loop();
			Arduboy_Port_RecordCpuLoad(arduboy.cpuLoad());
		}

		s_state = ARDUBOY_RUNTIME_STOPPING;
		Arduboy_Port_End();
		if (lvgl_start_ui() == SYSTEM_STATUS_OK)
		{
			s_result = ARDUBOY_RUNTIME_RESULT_EXITED;
			s_state = ARDUBOY_RUNTIME_FINISHED;
		}
		else
		{
			s_result = ARDUBOY_RUNTIME_RESULT_DISPLAY_RESUME_FAILED;
			s_state = ARDUBOY_RUNTIME_ERROR;
		}
	}
}
