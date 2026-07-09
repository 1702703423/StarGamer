#include "GUI_NES.h"
#include "GUI_FileManage.h"
#include "System_Status.h"
#include "nes_main.h"
#include "cmsis_os2.h"
#include "FreeRTOS.h"
#include "task.h"
#include "Malloc.h"

#define GUI_NES_THREAD_STACK_WORDS      2048U
#define GUI_NES_THREAD_STACK_SIZE       (GUI_NES_THREAD_STACK_WORDS * sizeof(u32))
#define GUI_NES_LAUNCH_DELAY_MS         80U
#define GUI_NES_RESULT_PENDING          0xFFU
#define GUI_NES_RESULT_STOP_FAILED      0xFEU
#define GUI_NES_RESULT_THREAD_FAILED    0xFDU

typedef enum
{
	GUI_NES_STATE_IDLE = 0,
	GUI_NES_STATE_STARTING,
	GUI_NES_STATE_RUNNING,
	GUI_NES_STATE_FINISHED,
	GUI_NES_STATE_ERROR
} GUI_NES_State_t;

typedef struct
{
	GUI_FileManage_t file_manage;
	char rom_path[GUI_FILEMANAGE_PATH_SIZE];
	volatile u8 state;
	volatile u8 active;
	volatile u8 result;
	osThreadId_t thread_id;
	lv_timer_t *poll_timer;
} GUI_NES_Context_t;

typedef StaticTask_t osStaticThreadDef_t;

static int GUI_NES_PageBuild(Page_Struct_t *page);
static int GUI_NES_PageLoaded(Page_Struct_t *page, void *user_data);
static int GUI_NES_OpenRom(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data);
static int GUI_NES_Back(GUI_FileManage_t *manager, void *user_data);
static void GUI_NES_RunTask(void *argument);
static void GUI_NES_PollTimer(lv_timer_t *timer);
static int GUI_NES_EnsureTimer(void);
static const char *GUI_NES_ResultText(u8 result);
static u8 GUI_NES_CopyText(char *dst, u32 dst_size, const char *src);

static GUI_NES_Context_t s_nes;
static const char * const s_nes_extensions[] =
{
	".nes"
};

static osStaticThreadDef_t s_nes_thread_cb;
static osThreadAttr_t s_nes_thread_attr =
{
	.name = "NES_Run",
	.cb_mem = &s_nes_thread_cb,
	.cb_size = sizeof(s_nes_thread_cb),
	.stack_mem = NULL,
	.stack_size = GUI_NES_THREAD_STACK_SIZE,
	.priority = (osPriority_t)osPriorityNormal,
};

int GUI_NES_Register(void)
{
	int code;

	if (Page_Find(GUI_PAGE_NES) != NULL)
	{
		return GUI_NES_EnsureTimer();
	}

	code = Page_Create(GUI_PAGE_NES, "NES", GUI_NES_PageBuild, GUI_NES_PageLoaded);
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	return GUI_NES_EnsureTimer();
}

int GUI_NES_Show(void)
{
	int code;

	if (Page_Find(GUI_PAGE_NES) == NULL)
	{
		code = GUI_NES_Register();
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	return Page_Show_Anim(GUI_PAGE_NES, LV_SCR_LOAD_ANIM_MOVE_LEFT, 120U, 0U);
}

static int GUI_NES_PageBuild(Page_Struct_t *page)
{
	GUI_FileManageConfig_t config;

	if (page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}

	memset(&s_nes, 0, sizeof(s_nes));
	memset(&config, 0, sizeof(config));

	config.title = "NES";
	config.root_path = "0:/";
	config.empty_text = "No NES ROMs";
	config.status_text = "ROM";
	config.extensions = s_nes_extensions;
	config.extension_count = (u8)(sizeof(s_nes_extensions) / sizeof(s_nes_extensions[0]));
	config.accent_color = 0xE84E40U;
	config.open = GUI_NES_OpenRom;
	config.back = GUI_NES_Back;
	config.user_data = &s_nes;

	s_nes.state = GUI_NES_STATE_IDLE;
	s_nes.result = GUI_NES_RESULT_PENDING;

	return GUI_FileManage_Create(&s_nes.file_manage, page, &config);
}

static int GUI_NES_PageLoaded(Page_Struct_t *page, void *user_data)
{
	(void)page;
	(void)user_data;

	return GUI_FileManage_PageLoaded(&s_nes.file_manage);
}

static int GUI_NES_OpenRom(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data)
{
	GUI_NES_Context_t *context = (GUI_NES_Context_t *)user_data;

	if (context == NULL || manager == NULL || item == NULL)
	{
		return OPERATE_FAILED;
	}

	if (context->active != 0U)
	{
		return OPERATE_FAILED;
	}

	if (GUI_NES_CopyText(context->rom_path, GUI_FILEMANAGE_PATH_SIZE, item->path) == 0U)
	{
		GUI_FileManage_SetStatus(manager, "Path too long");
		return OPERATE_FAILED;
	}

	GUI_FileManage_SetLocked(manager, 1U);
	GUI_FileManage_SetStatus(manager, "Starting NES");
	context->result = GUI_NES_RESULT_PENDING;
	context->state = GUI_NES_STATE_STARTING;
	context->active = 1U;

	if (s_nes_thread_attr.stack_mem == NULL)
	{
		s_nes_thread_attr.stack_mem = sys_thread_stack_alloc((u32)s_nes_thread_attr.stack_size);
	}
	if (s_nes_thread_attr.stack_mem == NULL)
	{
		context->result = GUI_NES_RESULT_THREAD_FAILED;
		context->state = GUI_NES_STATE_ERROR;
		context->active = 0U;
		GUI_FileManage_SetLocked(manager, 0U);
		GUI_FileManage_SetStatus(manager, GUI_NES_ResultText(context->result));
		return OUT_OF_HEAP_MEMORY;
	}

	context->thread_id = osThreadNew(GUI_NES_RunTask, context, &s_nes_thread_attr);
	if (context->thread_id == NULL)
	{
		context->result = GUI_NES_RESULT_THREAD_FAILED;
		context->state = GUI_NES_STATE_ERROR;
		context->active = 0U;
		GUI_FileManage_SetLocked(manager, 0U);
		GUI_FileManage_SetStatus(manager, GUI_NES_ResultText(context->result));
		return OUT_OF_HEAP_MEMORY;
	}

	return SYSTEM_STATUS_OK;
}

static int GUI_NES_Back(GUI_FileManage_t *manager, void *user_data)
{
	GUI_NES_Context_t *context = (GUI_NES_Context_t *)user_data;

	if (context != NULL && context->active != 0U)
	{
		if (manager != NULL)
		{
			GUI_FileManage_SetStatus(manager, "NES running");
		}
		return OPERATE_FAILED;
	}

	return Page_Back();
}

static void GUI_NES_RunTask(void *argument)
{
	GUI_NES_Context_t *context = (GUI_NES_Context_t *)argument;
	u8 result;

	if (context == NULL)
	{
		osThreadExit();
	}

	osDelay(GUI_NES_LAUNCH_DELAY_MS);
	context->state = GUI_NES_STATE_RUNNING;

	if (lvgl_stop_ui() == SYSTEM_STATUS_OK)
	{
		result = nes_load((u8 *)context->rom_path);
		(void)lvgl_start_ui();
	}
	else
	{
		result = GUI_NES_RESULT_STOP_FAILED;
	}

	context->result = result;
	context->state = (result == 0U) ? GUI_NES_STATE_FINISHED : GUI_NES_STATE_ERROR;
	context->active = 0U;
	context->thread_id = NULL;

	osThreadExit();
}

static void GUI_NES_PollTimer(lv_timer_t *timer)
{
	u8 state;
	u8 result;

	(void)timer;

	if (s_nes.file_manage.screen == NULL)
	{
		return;
	}

	state = s_nes.state;
	if (state == GUI_NES_STATE_STARTING)
	{
		GUI_FileManage_SetLocked(&s_nes.file_manage, 1U);
		GUI_FileManage_SetStatus(&s_nes.file_manage, "Starting NES");
	}
	else if (state == GUI_NES_STATE_RUNNING)
	{
		GUI_FileManage_SetLocked(&s_nes.file_manage, 1U);
		GUI_FileManage_SetStatus(&s_nes.file_manage, "NES running");
	}
	else if (state == GUI_NES_STATE_FINISHED || state == GUI_NES_STATE_ERROR)
	{
		result = s_nes.result;
		GUI_FileManage_SetLocked(&s_nes.file_manage, 0U);
		GUI_FileManage_SetStatus(&s_nes.file_manage, GUI_NES_ResultText(result));
		s_nes.state = GUI_NES_STATE_IDLE;

		if (Page_GetCurrent() == GUI_PAGE_NES)
		{
			(void)GUI_FileManage_PageLoaded(&s_nes.file_manage);
			lv_obj_invalidate(s_nes.file_manage.screen);
		}
	}
}

static int GUI_NES_EnsureTimer(void)
{
	if (s_nes.poll_timer != NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	s_nes.poll_timer = lv_timer_create(GUI_NES_PollTimer, 100U, NULL);
	if (s_nes.poll_timer == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	return SYSTEM_STATUS_OK;
}

static const char *GUI_NES_ResultText(u8 result)
{
	switch (result)
	{
		case 0U:
			return "NES exited";
		case 1U:
			return "NES memory error";
		case 2U:
			return "NES file error";
		case 3U:
			return "Unsupported mapper";
		case GUI_NES_RESULT_STOP_FAILED:
			return "LVGL stop failed";
		case GUI_NES_RESULT_THREAD_FAILED:
			return "NES thread failed";
		default:
			return "NES error";
	}
}

static u8 GUI_NES_CopyText(char *dst, u32 dst_size, const char *src)
{
	u32 len;

	if (dst == NULL || dst_size == 0U || src == NULL)
	{
		return 0U;
	}

	len = (u32)strlen(src);
	if (len >= dst_size)
	{
		return 0U;
	}

	memcpy(dst, src, len + 1U);
	return 1U;
}
