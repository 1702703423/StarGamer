#include "GUI_Axf.h"

#include "Extern_API.h"
#include "FreeRTOS.h"
#include "GUI_FileManage.h"
#include "GUI_Theme.h"
#include "Malloc.h"
#include "System_Status.h"
#include "dl_lib.h"
#include "key.h"
#include "task.h"

#include <stdio.h>
#include <string.h>

#define GUI_AXF_LAUNCH_DELAY_MS        80U
#define GUI_AXF_MONITOR_POLL_MS        10U
#define GUI_AXF_EXIT_GRACE_MS          100U
#define GUI_AXF_POLL_MS                100U
#define GUI_AXF_CLEANUP_DELAY_TICKS    2U
#define GUI_AXF_MONITOR_STACK_SIZE     1536U

typedef enum GUI_AxfState
{
	GUI_AXF_STATE_IDLE = 0,
	GUI_AXF_STATE_LOADING,
	GUI_AXF_STATE_STARTING,
	GUI_AXF_STATE_RUNNING,
	GUI_AXF_STATE_STOPPING,
	GUI_AXF_STATE_FINISHED,
	GUI_AXF_STATE_ERROR
} GUI_AxfState_t;

typedef enum GUI_AxfResult
{
	GUI_AXF_RESULT_NONE = 0,
	GUI_AXF_RESULT_COMPLETED,
	GUI_AXF_RESULT_EXIT_KEY,
	GUI_AXF_RESULT_LOAD_FAILED,
	GUI_AXF_RESULT_NO_ENTRY,
	GUI_AXF_RESULT_STACK_FAILED,
	GUI_AXF_RESULT_MONITOR_FAILED,
	GUI_AXF_RESULT_THREAD_FAILED,
	GUI_AXF_RESULT_API_FAILED,
	GUI_AXF_RESULT_DISPLAY_RECOVER_FAILED
} GUI_AxfResult_t;

typedef struct GUI_AxfContext
{
	GUI_FileManage_t file_manage;
	char path[GUI_FILEMANAGE_PATH_SIZE];
	DL_Handler handler;
	int (*entry)(int argc, char *argv[]);
	lv_timer_t *poll_timer;
	volatile osThreadId_t app_thread_id;
	volatile osThreadId_t monitor_thread_id;
	volatile GUI_AxfState_t state;
	volatile GUI_AxfResult_t result;
	volatile s32 load_error;
	volatile s32 entry_result;
	volatile u8 handler_loaded;
	volatile u8 stop_requested;
	volatile u8 monitor_abort;
	volatile u8 app_done;
	volatile u8 monitor_done;
	volatile u32 app_exit_tick;
	volatile u32 monitor_exit_tick;
	volatile u32 stack_space;
	u32 stack_size;
} GUI_AxfContext_t;

typedef StaticTask_t osStaticThreadDef_t;

static int GUI_Axf_PageBuild(Page_Struct_t *page);
static int GUI_Axf_PageLoaded(Page_Struct_t *page, void *user_data);
static GUI_AxfContext_t *GUI_Axf_GetContext(void);
static int GUI_Axf_EnsureTimer(void);
static int GUI_Axf_OpenFile(GUI_FileManage_t *manager,
	const GUI_FileManageItem_t *item, void *user_data);
static int GUI_Axf_Back(GUI_FileManage_t *manager, void *user_data);
static void GUI_Axf_RunTask(void *argument);
static void GUI_Axf_MonitorTask(void *argument);
static void GUI_Axf_PollTimer(lv_timer_t *timer);
static void GUI_Axf_Cleanup(GUI_AxfContext_t *context);
static void GUI_Axf_DestroyImage(GUI_AxfContext_t *context);
static u8 GUI_Axf_IsBusy(const GUI_AxfContext_t *context);
static const char *GUI_Axf_LoadErrorText(s32 error);
static void GUI_Axf_ResultText(const GUI_AxfContext_t *context,
	char *text, u32 text_size);
static void GUI_Axf_MarkAppDone(GUI_AxfContext_t *context);
static void GUI_Axf_MarkMonitorDone(GUI_AxfContext_t *context);

static GUI_AxfContext_t *s_axf;
static const char * const s_axf_extensions[] =
{
	".axf"
};

static osStaticThreadDef_t s_axf_thread_cb;
static osThreadAttr_t s_axf_thread_attr =
{
	.name = "AXF_App",
	.cb_mem = &s_axf_thread_cb,
	.cb_size = sizeof(s_axf_thread_cb),
	.stack_mem = NULL,
	.stack_size = 16U * 1024U,
	.priority = (osPriority_t)osPriorityBelowNormal,
};

static osStaticThreadDef_t s_axf_monitor_cb;
static osThreadAttr_t s_axf_monitor_attr =
{
	.name = "AXF_Exit",
	.cb_mem = &s_axf_monitor_cb,
	.cb_size = sizeof(s_axf_monitor_cb),
	.stack_mem = NULL,
	.stack_size = GUI_AXF_MONITOR_STACK_SIZE,
	.priority = (osPriority_t)osPriorityBelowNormal,
};

int GUI_Axf_Register(void)
{
	int code;

	if (GUI_Axf_GetContext() == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}
	if (Page_Find(GUI_PAGE_AXF_LOADER) != NULL)
	{
		return GUI_Axf_EnsureTimer();
	}

	code = Page_Create(GUI_PAGE_AXF_LOADER, "AXF Loader",
	                   GUI_Axf_PageBuild, GUI_Axf_PageLoaded);
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	return GUI_Axf_EnsureTimer();
}

int GUI_Axf_Show(void)
{
	int code;

	if (Page_Find(GUI_PAGE_AXF_LOADER) == NULL)
	{
		code = GUI_Axf_Register();
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	return Page_Show_Anim(GUI_PAGE_AXF_LOADER,
	                      LV_SCR_LOAD_ANIM_MOVE_LEFT, GUI_M3_MOTION_PAGE, 0U);
}

static int GUI_Axf_PageBuild(Page_Struct_t *page)
{
	GUI_AxfContext_t *context;
	GUI_FileManageConfig_t config;

	if (page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}
	context = GUI_Axf_GetContext();
	if (context == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	memset(context, 0, sizeof(*context));
	memset(&config, 0, sizeof(config));
	config.title = "AXF Loader";
	config.root_path = "0:/";
	config.empty_text = "No AXF files";
	config.extensions = s_axf_extensions;
	config.extension_count = (u8)(sizeof(s_axf_extensions) /
	                              sizeof(s_axf_extensions[0]));
	config.accent_color = GUI_Theme_Color(GUI_THEME_COLOR_PRIMARY);
	config.open = GUI_Axf_OpenFile;
	config.back = GUI_Axf_Back;
	config.user_data = context;
	context->state = GUI_AXF_STATE_IDLE;

	return GUI_FileManage_Create(&context->file_manage, page, &config);
}

static int GUI_Axf_PageLoaded(Page_Struct_t *page, void *user_data)
{
	GUI_AxfContext_t *context = s_axf;

	(void)page;
	(void)user_data;
	if (context == NULL)
	{
		return OPERATE_FAILED;
	}
	if (GUI_Axf_IsBusy(context) != 0U)
	{
		GUI_FileManage_SetLocked(&context->file_manage, 1U);
	}

	return GUI_FileManage_PageLoaded(&context->file_manage);
}

static GUI_AxfContext_t *GUI_Axf_GetContext(void)
{
	if (s_axf == NULL)
	{
		s_axf = (GUI_AxfContext_t *)sys_malloc(SRAMEX,
		                                      sizeof(GUI_AxfContext_t));
		if (s_axf != NULL)
		{
			memset(s_axf, 0, sizeof(*s_axf));
		}
	}

	return s_axf;
}

static int GUI_Axf_EnsureTimer(void)
{
	GUI_AxfContext_t *context = GUI_Axf_GetContext();

	if (context == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}
	if (context->poll_timer != NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	context->poll_timer = lv_timer_create(GUI_Axf_PollTimer,
	                                      GUI_AXF_POLL_MS, context);
	return (context->poll_timer != NULL) ? SYSTEM_STATUS_OK : OUT_OF_HEAP_MEMORY;
}

static int GUI_Axf_OpenFile(GUI_FileManage_t *manager,
	const GUI_FileManageItem_t *item, void *user_data)
{
	GUI_AxfContext_t *context = (GUI_AxfContext_t *)user_data;
	DL_Err_Type load_error;

	if (manager == NULL || item == NULL || context == NULL)
	{
		return OPERATE_FAILED;
	}
	GUI_Axf_Cleanup(context);
	if (GUI_Axf_IsBusy(context) != 0U || context->app_thread_id != NULL ||
	    context->monitor_thread_id != NULL || context->handler_loaded != 0U)
	{
		(void)GUI_FileManage_ReportError(manager, "AXF is busy");
		return OPERATE_FAILED;
	}
	if (GUI_FileManage_BuildItemPath(manager, item, context->path,
	                                sizeof(context->path)) == 0U)
	{
		(void)GUI_FileManage_ReportError(manager, "Path too long");
		return OPERATE_FAILED;
	}

	GUI_FileManage_SetLocked(manager, 1U);
	(void)GUI_FileManage_Notify(manager, "Loading AXF");
	context->state = GUI_AXF_STATE_LOADING;
	context->result = GUI_AXF_RESULT_NONE;
	context->load_error = DL_NO_ERR;
	context->entry_result = 0;
	context->stop_requested = 0U;
	context->monitor_abort = 0U;
	context->app_done = 0U;
	context->monitor_done = 0U;
	context->app_exit_tick = 0U;
	context->monitor_exit_tick = 0U;
	context->stack_space = 0U;
	memset(&context->handler, 0, sizeof(context->handler));

	load_error = dl_load_file(&context->handler, context->path);
	if (load_error != DL_NO_ERR)
	{
		context->load_error = (s32)load_error;
		context->result = GUI_AXF_RESULT_LOAD_FAILED;
		context->state = GUI_AXF_STATE_ERROR;
		return OPERATE_FAILED;
	}
	context->handler_loaded = 1U;
	context->entry = dl_get_entry(&context->handler);
	if (context->entry == NULL)
	{
		context->result = GUI_AXF_RESULT_NO_ENTRY;
		context->state = GUI_AXF_STATE_ERROR;
		GUI_Axf_DestroyImage(context);
		return OPERATE_FAILED;
	}

	context->stack_size = System_Settings_AxfStackSize();
	s_axf_thread_attr.stack_size = context->stack_size;
	s_axf_thread_attr.stack_mem = sys_thread_stack_alloc(context->stack_size);
	if (s_axf_thread_attr.stack_mem == NULL)
	{
		context->result = GUI_AXF_RESULT_STACK_FAILED;
		context->state = GUI_AXF_STATE_ERROR;
		GUI_Axf_DestroyImage(context);
		return OUT_OF_HEAP_MEMORY;
	}

	s_axf_monitor_attr.stack_mem = sys_thread_stack_alloc(
		(u32)s_axf_monitor_attr.stack_size);
	if (s_axf_monitor_attr.stack_mem == NULL)
	{
		sys_thread_stack_free(s_axf_thread_attr.stack_mem);
		s_axf_thread_attr.stack_mem = NULL;
		context->result = GUI_AXF_RESULT_MONITOR_FAILED;
		context->state = GUI_AXF_STATE_ERROR;
		GUI_Axf_DestroyImage(context);
		return OUT_OF_HEAP_MEMORY;
	}

	context->state = GUI_AXF_STATE_STARTING;
	context->monitor_thread_id = SysCreateThread(GUI_Axf_MonitorTask,
		context, &s_axf_monitor_attr, 0U);
	if (context->monitor_thread_id == NULL)
	{
		sys_thread_stack_free(s_axf_monitor_attr.stack_mem);
		s_axf_monitor_attr.stack_mem = NULL;
		sys_thread_stack_free(s_axf_thread_attr.stack_mem);
		s_axf_thread_attr.stack_mem = NULL;
		context->result = GUI_AXF_RESULT_MONITOR_FAILED;
		context->state = GUI_AXF_STATE_ERROR;
		GUI_Axf_DestroyImage(context);
		return OPERATE_FAILED;
	}

	context->app_thread_id = SysCreateThread(GUI_Axf_RunTask,
		context, &s_axf_thread_attr, 1U);
	if (context->app_thread_id == NULL)
	{
		context->monitor_abort = 1U;
		sys_thread_stack_free(s_axf_thread_attr.stack_mem);
		s_axf_thread_attr.stack_mem = NULL;
		context->result = GUI_AXF_RESULT_THREAD_FAILED;
		context->state = GUI_AXF_STATE_ERROR;
		GUI_Axf_DestroyImage(context);
		return OPERATE_FAILED;
	}

	__DMB();
	return SYSTEM_STATUS_OK;
}

static int GUI_Axf_Back(GUI_FileManage_t *manager, void *user_data)
{
	GUI_AxfContext_t *context = (GUI_AxfContext_t *)user_data;

	if (context != NULL && GUI_Axf_IsBusy(context) != 0U)
	{
		context->stop_requested = 1U;
		__DMB();
		if (manager != NULL)
		{
			(void)GUI_FileManage_Notify(manager, "Stopping AXF");
		}
		return OPERATE_FAILED;
	}

	return Page_Back();
}

static void GUI_Axf_RunTask(void *argument)
{
	GUI_AxfContext_t *context = (GUI_AxfContext_t *)argument;
	osThreadId_t self = osThreadGetId();
	int api_code;
	u8 exit_requested;

	if (context == NULL || self == NULL)
	{
		osThreadExit();
		return;
	}
	while (context->app_thread_id != self && context->monitor_abort == 0U)
	{
		(void)osDelay(1U);
	}
	if (context->monitor_abort != 0U ||
	    ExternAPI_AXF_Begin(self) != SYSTEM_STATUS_OK)
	{
		context->result = GUI_AXF_RESULT_API_FAILED;
		context->state = GUI_AXF_STATE_ERROR;
		(void)SysUnregisterThread(self);
		GUI_Axf_MarkAppDone(context);
		osThreadExit();
		return;
	}

	(void)osDelay(GUI_AXF_LAUNCH_DELAY_MS);
	context->state = GUI_AXF_STATE_RUNNING;
	__DMB();
	context->entry_result = (s32)context->entry(0, NULL);
	context->stack_space = (u32)uxTaskGetStackHighWaterMark(NULL) *
	                       (u32)sizeof(StackType_t);
	exit_requested = ExternAPI_AXF_ShouldExit();
	api_code = ExternAPI_AXF_End(self);
	if (api_code == SYSTEM_STATUS_OK)
	{
		context->result = (exit_requested != 0U || context->stop_requested != 0U) ?
		                  GUI_AXF_RESULT_EXIT_KEY : GUI_AXF_RESULT_COMPLETED;
		context->state = GUI_AXF_STATE_FINISHED;
	}
	else
	{
		context->result = GUI_AXF_RESULT_DISPLAY_RECOVER_FAILED;
		context->state = GUI_AXF_STATE_ERROR;
	}
	(void)SysUnregisterThread(self);
	GUI_Axf_MarkAppDone(context);
	osThreadExit();
}

static void GUI_Axf_MonitorTask(void *argument)
{
	GUI_AxfContext_t *context = (GUI_AxfContext_t *)argument;
	osThreadId_t self = osThreadGetId();
	osThreadId_t app_thread;
	u8 exit_armed = 0U;
	u32 grace_elapsed;
	int terminate_code;
	int recover_code;

	if (context == NULL || self == NULL)
	{
		osThreadExit();
		return;
	}
	while (context->monitor_thread_id != self && context->monitor_abort == 0U)
	{
		(void)osDelay(1U);
	}
	while (context->app_thread_id == NULL && context->monitor_abort == 0U)
	{
		(void)osDelay(1U);
	}

	while (context->monitor_abort == 0U && context->app_done == 0U)
	{
		if ((KEY_GetState() & KEY_GAME_EXIT) == 0U)
		{
			exit_armed = 1U;
		}
		if ((exit_armed != 0U &&
		     (KEY_GetState() & KEY_GAME_EXIT) != 0U) ||
		    context->stop_requested != 0U)
		{
			context->state = GUI_AXF_STATE_STOPPING;
			app_thread = context->app_thread_id;
			ExternAPI_AXF_RequestExit(app_thread);
			grace_elapsed = 0U;
			while (context->app_done == 0U &&
			       grace_elapsed < GUI_AXF_EXIT_GRACE_MS)
			{
				(void)osDelay(GUI_AXF_MONITOR_POLL_MS);
				grace_elapsed += GUI_AXF_MONITOR_POLL_MS;
			}
			if (context->app_done != 0U)
			{
				break;
			}
			terminate_code = SysTerminateThread(app_thread);
			if (terminate_code == SYSTEM_STATUS_OK)
			{
				recover_code = ExternAPI_AXF_ForceEnd(app_thread);
				context->result = (recover_code == SYSTEM_STATUS_OK) ?
				                  GUI_AXF_RESULT_EXIT_KEY :
				                  GUI_AXF_RESULT_DISPLAY_RECOVER_FAILED;
				context->state = (recover_code == SYSTEM_STATUS_OK) ?
				                 GUI_AXF_STATE_FINISHED : GUI_AXF_STATE_ERROR;
				GUI_Axf_MarkAppDone(context);
			}
			else
			{
				/* The app may have unregistered itself just before this request. */
				while (context->app_done == 0U)
				{
					(void)osDelay(1U);
				}
			}
			break;
		}
		(void)osDelay(GUI_AXF_MONITOR_POLL_MS);
	}

	(void)SysUnregisterThread(self);
	GUI_Axf_MarkMonitorDone(context);
	osThreadExit();
}

static void GUI_Axf_PollTimer(lv_timer_t *timer)
{
	GUI_AxfContext_t *context = (GUI_AxfContext_t *)timer->user_data;
	char status[64];
	GUI_AxfState_t state;

	if (context == NULL || context->file_manage.screen == NULL)
	{
		return;
	}
	GUI_Axf_Cleanup(context);
	state = context->state;
	if (state == GUI_AXF_STATE_LOADING)
	{
		GUI_FileManage_SetLocked(&context->file_manage, 1U);
		(void)GUI_FileManage_Notify(&context->file_manage, "Loading AXF");
	}
	else if (state == GUI_AXF_STATE_STARTING)
	{
		GUI_FileManage_SetLocked(&context->file_manage, 1U);
		(void)GUI_FileManage_Notify(&context->file_manage, "Starting AXF");
	}
	else if (state == GUI_AXF_STATE_RUNNING)
	{
		GUI_FileManage_SetLocked(&context->file_manage, 1U);
		(void)GUI_FileManage_Notify(&context->file_manage, "AXF running - PB7 exits");
	}
	else if (state == GUI_AXF_STATE_STOPPING)
	{
		GUI_FileManage_SetLocked(&context->file_manage, 1U);
		(void)GUI_FileManage_Notify(&context->file_manage, "Stopping AXF");
	}
	else if ((state == GUI_AXF_STATE_FINISHED || state == GUI_AXF_STATE_ERROR) &&
	         context->app_thread_id == NULL && context->monitor_thread_id == NULL &&
	         context->handler_loaded == 0U)
	{
		GUI_Axf_ResultText(context, status, sizeof(status));
		GUI_FileManage_SetLocked(&context->file_manage, 0U);
		if (state == GUI_AXF_STATE_ERROR)
		{
			(void)GUI_FileManage_ReportError(&context->file_manage, status);
		}
		else
		{
			(void)GUI_FileManage_Notify(&context->file_manage, status);
		}
		context->state = GUI_AXF_STATE_IDLE;
		if (Page_GetCurrent() == GUI_PAGE_AXF_LOADER)
		{
			(void)GUI_FileManage_PageLoaded(&context->file_manage);
			lv_obj_invalidate(context->file_manage.screen);
		}
	}
}

static void GUI_Axf_Cleanup(GUI_AxfContext_t *context)
{
	u32 now;

	if (context == NULL)
	{
		return;
	}
	now = osKernelGetTickCount();
	if (context->app_thread_id != NULL && context->app_done != 0U &&
	    context->app_exit_tick != 0U &&
	    (now - context->app_exit_tick) >= GUI_AXF_CLEANUP_DELAY_TICKS)
	{
		if (s_axf_thread_attr.stack_mem != NULL)
		{
			sys_thread_stack_free(s_axf_thread_attr.stack_mem);
			s_axf_thread_attr.stack_mem = NULL;
		}
		context->app_thread_id = NULL;
		context->app_exit_tick = 0U;
	}
	if (context->monitor_thread_id != NULL && context->monitor_done != 0U &&
	    context->monitor_exit_tick != 0U &&
	    (now - context->monitor_exit_tick) >= GUI_AXF_CLEANUP_DELAY_TICKS)
	{
		if (s_axf_monitor_attr.stack_mem != NULL)
		{
			sys_thread_stack_free(s_axf_monitor_attr.stack_mem);
			s_axf_monitor_attr.stack_mem = NULL;
		}
		context->monitor_thread_id = NULL;
		context->monitor_exit_tick = 0U;
	}
	if (context->app_thread_id == NULL && context->monitor_thread_id == NULL &&
	    context->handler_loaded != 0U)
	{
		GUI_Axf_DestroyImage(context);
	}
}

static void GUI_Axf_DestroyImage(GUI_AxfContext_t *context)
{
	if (context == NULL || context->handler_loaded == 0U)
	{
		return;
	}

	dl_destroy_lib(&context->handler);
	memset(&context->handler, 0, sizeof(context->handler));
	context->handler_loaded = 0U;
	context->entry = NULL;
}

static u8 GUI_Axf_IsBusy(const GUI_AxfContext_t *context)
{
	if (context == NULL)
	{
		return 0U;
	}

	return (context->state == GUI_AXF_STATE_LOADING ||
	        context->state == GUI_AXF_STATE_STARTING ||
	        context->state == GUI_AXF_STATE_RUNNING ||
	        context->state == GUI_AXF_STATE_STOPPING) ? 1U : 0U;
}

static const char *GUI_Axf_LoadErrorText(s32 error)
{
	switch ((DL_Err_Type)error)
	{
		case DL_FILE_INVALID:
			return "Invalid AXF";
		case DL_MALLOC_FAIL:
			return "AXF memory failed";
		case DL_MEMSIZE_ERR:
			return "Invalid AXF memory";
		case DL_RELOCATE_ERR:
			return "AXF relocation failed";
		case DL_UNDEF_SYM_ERR:
			return "AXF symbol missing";
		case DL_FILE_OPEN_ERR:
			return "AXF read failed";
		case DL_PLATFORM_ERR:
			return "AXF platform mismatch";
		case DL_ELF_NOT_SUPPRORT:
			return "Unsupported AXF";
		case DL_ELF_NO_ENTRY:
			return "AXF has no entry";
		default:
			return "AXF load failed";
	}
}

static void GUI_Axf_ResultText(const GUI_AxfContext_t *context,
	char *text, u32 text_size)
{
	if (context == NULL || text == NULL || text_size == 0U)
	{
		return;
	}

	switch (context->result)
	{
		case GUI_AXF_RESULT_COMPLETED:
			(void)snprintf(text, text_size, "AXF exited: %ld",
			               (long)context->entry_result);
			break;
		case GUI_AXF_RESULT_EXIT_KEY:
			(void)snprintf(text, text_size, "AXF stopped by PB7");
			break;
		case GUI_AXF_RESULT_LOAD_FAILED:
			(void)snprintf(text, text_size, "%s",
			               GUI_Axf_LoadErrorText(context->load_error));
			break;
		case GUI_AXF_RESULT_NO_ENTRY:
			(void)snprintf(text, text_size, "AXF has no entry");
			break;
		case GUI_AXF_RESULT_STACK_FAILED:
			(void)snprintf(text, text_size, "AXF stack allocation failed");
			break;
		case GUI_AXF_RESULT_MONITOR_FAILED:
			(void)snprintf(text, text_size, "AXF monitor failed");
			break;
		case GUI_AXF_RESULT_THREAD_FAILED:
			(void)snprintf(text, text_size, "AXF thread failed");
			break;
		case GUI_AXF_RESULT_API_FAILED:
			(void)snprintf(text, text_size, "AXF API setup failed");
			break;
		case GUI_AXF_RESULT_DISPLAY_RECOVER_FAILED:
			(void)snprintf(text, text_size, "LCD recovery failed");
			break;
		default:
			(void)snprintf(text, text_size, "AXF ready");
			break;
	}
}

static void GUI_Axf_MarkAppDone(GUI_AxfContext_t *context)
{
	context->app_exit_tick = osKernelGetTickCount();
	context->app_done = 1U;
	__DMB();
}

static void GUI_Axf_MarkMonitorDone(GUI_AxfContext_t *context)
{
	context->monitor_exit_tick = osKernelGetTickCount();
	context->monitor_done = 1U;
	__DMB();
}
