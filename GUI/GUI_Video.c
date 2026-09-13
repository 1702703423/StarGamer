#include "GUI_Video.h"

#include "GUI_FileManage.h"
#include "GUI_Theme.h"
#include "System_Status.h"
#include "Video.h"
#include "Video_Memory.h"

#include <string.h>

#define GUI_VIDEO_POLL_MS   100U

typedef struct GUI_VideoContext
{
	GUI_FileManage_t file_manage;
	char path[GUI_FILEMANAGE_PATH_SIZE];
	lv_timer_t *poll_timer;
	u8 active;
} GUI_VideoContext_t;

static int GUI_Video_PageBuild(Page_Struct_t *page);
static int GUI_Video_PageLoaded(Page_Struct_t *page, void *user_data);
static GUI_VideoContext_t *GUI_Video_GetContext(void);
static int GUI_Video_OpenFile(GUI_FileManage_t *manager,
	const GUI_FileManageItem_t *item, void *user_data);
static int GUI_Video_Back(GUI_FileManage_t *manager, void *user_data);
static void GUI_Video_PollTimer(lv_timer_t *timer);
static const char *GUI_Video_StateText(Video_State_t state);

static GUI_VideoContext_t *s_video_gui;
static const char * const s_video_extensions[] =
{
	".avi"
};

int GUI_Video_Register(void)
{
	int code;

	code = Video_Init();
	if(code != SYSTEM_STATUS_OK)
	{
		return code;
	}
	if(Page_Find(GUI_PAGE_VIDEO) != NULL)
	{
		return SYSTEM_STATUS_OK;
	}
	if(GUI_Video_GetContext() == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}
	return Page_Create(GUI_PAGE_VIDEO, "Video", GUI_Video_PageBuild,
	                   GUI_Video_PageLoaded);
}

int GUI_Video_Show(void)
{
	int code;

	if(Page_Find(GUI_PAGE_VIDEO) == NULL)
	{
		code = GUI_Video_Register();
		if(code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}
	return Page_Show_Anim(GUI_PAGE_VIDEO, LV_SCR_LOAD_ANIM_MOVE_LEFT,
	                      GUI_M3_MOTION_PAGE, 0U);
}

static int GUI_Video_PageBuild(Page_Struct_t *page)
{
	GUI_VideoContext_t *context;
	GUI_FileManageConfig_t config;
	int code;

	if(page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}
	context = GUI_Video_GetContext();
	if(context == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}
	memset(context, 0, sizeof(*context));
	memset(&config, 0, sizeof(config));

	config.title = "Video";
	config.root_path = "0:/";
	config.empty_text = "No AVI files";
	config.extensions = s_video_extensions;
	config.extension_count = (u8)(sizeof(s_video_extensions) /
	                              sizeof(s_video_extensions[0]));
	config.accent_color = GUI_Theme_Color(GUI_THEME_COLOR_PRIMARY);
	config.open = GUI_Video_OpenFile;
	config.back = GUI_Video_Back;
	config.user_data = context;

	code = GUI_FileManage_Create(&context->file_manage, page, &config);
	if(code != SYSTEM_STATUS_OK)
	{
		return code;
	}
	context->poll_timer = lv_timer_create(GUI_Video_PollTimer,
	                                      GUI_VIDEO_POLL_MS, context);
	return (context->poll_timer != NULL) ? SYSTEM_STATUS_OK : OUT_OF_HEAP_MEMORY;
}

static int GUI_Video_PageLoaded(Page_Struct_t *page, void *user_data)
{
	GUI_VideoContext_t *context = s_video_gui;

	(void)page;
	(void)user_data;
	if(context == NULL)
	{
		return OPERATE_FAILED;
	}
	if(context->active != 0U)
	{
		GUI_FileManage_SetLocked(&context->file_manage, 1U);
		(void)GUI_FileManage_Notify(&context->file_manage,
		                            GUI_Video_StateText(Video_GetState()));
	}
	return GUI_FileManage_PageLoaded(&context->file_manage);
}

static GUI_VideoContext_t *GUI_Video_GetContext(void)
{
	if(s_video_gui == NULL)
	{
		s_video_gui = (GUI_VideoContext_t *)Video_Memory_Alloc(
			sizeof(GUI_VideoContext_t));
		if(s_video_gui != NULL)
		{
			memset(s_video_gui, 0, sizeof(*s_video_gui));
		}
	}
	return s_video_gui;
}

static int GUI_Video_OpenFile(GUI_FileManage_t *manager,
	const GUI_FileManageItem_t *item, void *user_data)
{
	GUI_VideoContext_t *context = (GUI_VideoContext_t *)user_data;
	int code;

	if(manager == NULL || item == NULL || context == NULL)
	{
		return OPERATE_FAILED;
	}
	Video_Cleanup();
	if(Video_IsBusy() != 0U || context->active != 0U)
	{
		(void)GUI_FileManage_ReportError(manager, "Video is busy");
		return OPERATE_FAILED;
	}
	if(GUI_FileManage_BuildItemPath(manager, item, context->path,
	                                sizeof(context->path)) == 0U)
	{
		(void)GUI_FileManage_ReportError(manager, "Path too long");
		return OPERATE_FAILED;
	}

	GUI_FileManage_SetLocked(manager, 1U);
	(void)GUI_FileManage_Notify(manager, "Opening AVI");
	code = Video_Play(context->path);
	if(code != SYSTEM_STATUS_OK)
	{
		GUI_FileManage_SetLocked(manager, 0U);
		(void)GUI_FileManage_ReportError(manager, Video_ResultText(Video_GetResult()));
		return code;
	}
	context->active = 1U;
	return SYSTEM_STATUS_OK;
}

static int GUI_Video_Back(GUI_FileManage_t *manager, void *user_data)
{
	GUI_VideoContext_t *context = (GUI_VideoContext_t *)user_data;

	if(context != NULL && (context->active != 0U || Video_IsBusy() != 0U))
	{
		(void)Video_Stop();
		if(manager != NULL)
		{
			(void)GUI_FileManage_Notify(manager, "Stopping video");
		}
		return OPERATE_FAILED;
	}
	return Page_Back();
}

static void GUI_Video_PollTimer(lv_timer_t *timer)
{
	GUI_VideoContext_t *context = (GUI_VideoContext_t *)timer->user_data;
	Video_State_t state;
	Video_Result_t result;

	if(context == NULL || context->active == 0U)
	{
		Video_Cleanup();
		return;
	}
	state = Video_GetState();
	if(state == VIDEO_STATE_FINISHED || state == VIDEO_STATE_ERROR)
	{
		result = Video_GetResult();
		context->active = 0U;
		GUI_FileManage_SetLocked(&context->file_manage, 0U);
		if(state == VIDEO_STATE_ERROR)
		{
			(void)GUI_FileManage_ReportError(&context->file_manage,
			                                 Video_ResultText(result));
		}
		else
		{
			(void)GUI_FileManage_Notify(&context->file_manage,
			                            Video_ResultText(result));
		}
		Video_Cleanup();
		if(Page_GetCurrent() == GUI_PAGE_VIDEO)
		{
			(void)GUI_FileManage_PageLoaded(&context->file_manage);
			lv_obj_invalidate(context->file_manage.screen);
		}
		return;
	}

	GUI_FileManage_SetLocked(&context->file_manage, 1U);
	(void)GUI_FileManage_Notify(&context->file_manage, GUI_Video_StateText(state));
}

static const char *GUI_Video_StateText(Video_State_t state)
{
	switch(state)
	{
		case VIDEO_STATE_OPENING:
			return "Opening AVI";
		case VIDEO_STATE_PREBUFFER:
			return "Preparing MJPEG";
		case VIDEO_STATE_PLAYING:
			return "Video playing";
		case VIDEO_STATE_PAUSED:
			return "Video paused";
		case VIDEO_STATE_STOPPING:
			return "Stopping video";
		default:
			return "MJPEG / PCM AVI";
	}
}
