#include "Arduboy_Runtime.h"

#include "FreeRTOS.h"
#include "Malloc.h"
#include "System_Status.h"
#include "arduboy.h"
#include "cmsis_os2.h"
#include "task.h"

#define ARDUBOY_RUNTIME_STACK_SIZE       4096U
#define ARDUBOY_RUNTIME_LAUNCH_DELAY_MS  80U
#define ARDUBOY_RUNTIME_POLL_MS          10U
#define ARDUBOY_RUNTIME_CLEANUP_DELAY_TICKS 2U
#define ARDUBOY_RUNTIME_NO_GAME          0xFFU

static void Arduboy_Runtime_Task(void *argument);
static int Arduboy_Runtime_EnsureCore(void);
static u8 Arduboy_Runtime_WaitForLaunch(void);
static void Arduboy_Runtime_CompleteExit(u8 ui_paused, u8 port_started);
static void Arduboy_Runtime_MarkThreadExit(void);

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
static volatile u32 s_thread_stack_space;
static volatile u32 s_thread_exit_tick;
static const Arduboy_Game_t *s_selected_game;
static u8 s_core_tested;

int Arduboy_Runtime_Start(u8 game_index)
{
	const Arduboy_Game_t *game;
	int code;

	Arduboy_Runtime_Cleanup();
	game = Arduboy_Games_Get(game_index);
	if (game == NULL)
	{
		s_result = ARDUBOY_RUNTIME_RESULT_INVALID_GAME;
		return OPERATE_FAILED;
	}

	if (Arduboy_Runtime_IsActive() != 0U || s_thread_id != NULL)
	{
		return OPERATE_FAILED;
	}

	code = Arduboy_Runtime_EnsureCore();
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	Arduboy_Port_ResetStop();
	s_selected_game = game;
	s_game_index = game_index;
	s_result = ARDUBOY_RUNTIME_RESULT_NONE;
	s_thread_stack_space = 0U;
	s_thread_exit_tick = 0U;
	s_state = ARDUBOY_RUNTIME_STARTING;
	__DMB();

	s_thread_attr.stack_mem = sys_thread_stack_alloc((u32)s_thread_attr.stack_size);
	if (s_thread_attr.stack_mem == NULL)
	{
		s_result = ARDUBOY_RUNTIME_RESULT_STACK_FAILED;
		s_state = ARDUBOY_RUNTIME_ERROR;
		return OUT_OF_HEAP_MEMORY;
	}

	s_thread_id = osThreadNew(Arduboy_Runtime_Task, NULL, &s_thread_attr);
	if (s_thread_id == NULL)
	{
		sys_thread_stack_free(s_thread_attr.stack_mem);
		s_thread_attr.stack_mem = NULL;
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

void Arduboy_Runtime_Cleanup(void)
{
	u32 now;

	if (s_thread_id == NULL || s_thread_exit_tick == 0U)
	{
		return;
	}

	now = osKernelGetTickCount();
	if ((now - s_thread_exit_tick) < ARDUBOY_RUNTIME_CLEANUP_DELAY_TICKS)
	{
		return;
	}

	if (s_thread_attr.stack_mem != NULL)
	{
		sys_thread_stack_free(s_thread_attr.stack_mem);
		s_thread_attr.stack_mem = NULL;
	}
	s_thread_id = NULL;
	s_thread_exit_tick = 0U;
	s_selected_game = NULL;
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
	if (s_thread_id != NULL && Arduboy_Runtime_IsActive() != 0U)
	{
		return osThreadGetStackSpace(s_thread_id);
	}

	return s_thread_stack_space;
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

static int Arduboy_Runtime_EnsureCore(void)
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

	return SYSTEM_STATUS_OK;
}

static void Arduboy_Runtime_Task(void *argument)
{
	const Arduboy_Game_t *game;
	int code;

	(void)argument;
	__DMB();
	game = s_selected_game;
	if (game == NULL)
	{
		s_result = ARDUBOY_RUNTIME_RESULT_INVALID_GAME;
		s_state = ARDUBOY_RUNTIME_ERROR;
		Arduboy_Runtime_MarkThreadExit();
		osThreadExit();
		return;
	}

	if (Arduboy_Runtime_WaitForLaunch() == 0U)
	{
		Arduboy_Runtime_CompleteExit(0U, 0U);
		Arduboy_Runtime_MarkThreadExit();
		osThreadExit();
		return;
	}

	code = lvgl_stop_ui();
	if (code != SYSTEM_STATUS_OK)
	{
		s_result = ARDUBOY_RUNTIME_RESULT_DISPLAY_BUSY;
		s_state = ARDUBOY_RUNTIME_ERROR;
		Arduboy_Runtime_MarkThreadExit();
		osThreadExit();
		return;
	}
	if (Arduboy_Port_ShouldStop() != 0U)
	{
		Arduboy_Runtime_CompleteExit(1U, 0U);
		Arduboy_Runtime_MarkThreadExit();
		osThreadExit();
		return;
	}

	code = Arduboy_Port_Begin();
	if (code != SYSTEM_STATUS_OK)
	{
		s_result = ARDUBOY_RUNTIME_RESULT_FRAMEBUFFER_FAILED;
		(void)lvgl_start_ui();
		s_state = ARDUBOY_RUNTIME_ERROR;
		Arduboy_Runtime_MarkThreadExit();
		osThreadExit();
		return;
	}
	if (Arduboy_Port_ShouldStop() != 0U)
	{
		Arduboy_Runtime_CompleteExit(1U, 1U);
		Arduboy_Runtime_MarkThreadExit();
		osThreadExit();
		return;
	}

	s_state = ARDUBOY_RUNTIME_RUNNING;
	game->init();
	while (Arduboy_Port_ShouldStop() == 0U)
	{
		game->loop();
		Arduboy_Port_RecordCpuLoad(arduboy.cpuLoad());
	}

	Arduboy_Runtime_CompleteExit(1U, 1U);
	Arduboy_Runtime_MarkThreadExit();
	osThreadExit();
}

static u8 Arduboy_Runtime_WaitForLaunch(void)
{
	u32 remaining = ARDUBOY_RUNTIME_LAUNCH_DELAY_MS;
	u32 delay;

	while (remaining > 0U)
	{
		if (Arduboy_Port_ShouldStop() != 0U)
		{
			return 0U;
		}

		delay = (remaining > ARDUBOY_RUNTIME_POLL_MS) ?
		        ARDUBOY_RUNTIME_POLL_MS : remaining;
		(void)osDelay(delay);
		remaining -= delay;
	}

	return (Arduboy_Port_ShouldStop() == 0U) ? 1U : 0U;
}

static void Arduboy_Runtime_CompleteExit(u8 ui_paused, u8 port_started)
{
	s_state = ARDUBOY_RUNTIME_STOPPING;
	if (port_started != 0U)
	{
		Arduboy_Port_End();
	}

	s_thread_stack_space = (u32)uxTaskGetStackHighWaterMark(NULL) * (u32)sizeof(StackType_t);
	s_result = ARDUBOY_RUNTIME_RESULT_EXITED;
	s_state = ARDUBOY_RUNTIME_FINISHED;
	__DMB();
	if (ui_paused != 0U && lvgl_start_ui() != SYSTEM_STATUS_OK)
	{
		s_result = ARDUBOY_RUNTIME_RESULT_DISPLAY_RESUME_FAILED;
		s_state = ARDUBOY_RUNTIME_ERROR;
		__DMB();
		return;
	}

	Arduboy_Port_WaitForExitRelease();
}

static void Arduboy_Runtime_MarkThreadExit(void)
{
	s_thread_stack_space = (u32)uxTaskGetStackHighWaterMark(NULL) * (u32)sizeof(StackType_t);
	__DMB();
	s_thread_exit_tick = osKernelGetTickCount();
	__DMB();
}
