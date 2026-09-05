#include "GUI_Player.h"
#include "GUI_FileManage.h"
#include "GUI_Font.h"
#include "GUI_Theme.h"
#include "Player.h"
#include "i2s.h"
#include "System_Status.h"
#include "Malloc.h"
#include "key.h"

#include <stdio.h>
#include <string.h>

/* Material Design 3 now-playing layout (320 x 240 panel, 40 px top app bar) */
#define GUI_PLAYER_LIST_Y              126
#define GUI_PLAYER_LIST_H              74
#define GUI_PLAYER_INFO_X              10
#define GUI_PLAYER_INFO_Y              62
#define GUI_PLAYER_INFO_W              300
#define GUI_PLAYER_INFO_H              60
#define GUI_PLAYER_CTRL_SIZE           GUI_M3_ICON_BTN_SIZE
#define GUI_PLAYER_CTRL_Y              204
#define GUI_PLAYER_PLAY_X              10
#define GUI_PLAYER_STOP_X              46
#define GUI_PLAYER_VOL_ICON_X          88
#define GUI_PLAYER_VOL_SLIDER_X        114
#define GUI_PLAYER_VOL_SLIDER_W        140
#define GUI_PLAYER_VOL_LABEL_X         262
#define GUI_PLAYER_VOL_LABEL_W         48
#define GUI_PLAYER_VOLUME_MAX          100
#define GUI_PLAYER_SEEK_MAX            1000
#define GUI_PLAYER_SEEK_KEY_STEP       25
#define GUI_PLAYER_SEEK_COMMIT_MS      180U
#define GUI_PLAYER_TIMER_MS            250U
#define GUI_PLAYER_NAME_UTF8_SIZE      (((GUI_FILEMANAGE_NAME_SIZE - 1U) * 3U / 2U) + 2U)

typedef struct
{
	GUI_FileManage_t file_manage;
	char current_path[GUI_FILEMANAGE_PATH_SIZE];
	char current_name[GUI_FILEMANAGE_NAME_SIZE];
	char current_name_utf8[GUI_PLAYER_NAME_UTF8_SIZE];
	lv_obj_t *info_panel;
	lv_obj_t *track_label;
	lv_obj_t *state_label;
	lv_obj_t *time_label;
	lv_obj_t *seek_slider;
	lv_obj_t *play_btn;
	lv_obj_t *play_icon;
	lv_obj_t *stop_btn;
	lv_obj_t *stop_icon;
	lv_obj_t *volume_icon;
	lv_obj_t *volume_slider;
	lv_obj_t *volume_label;
	lv_timer_t *poll_timer;
	lv_timer_t *seek_commit_timer;
	u8 volume;
	u8 seek_updating;
	u8 seek_pressed;
	u8 seek_commit_pending;
	u8 seek_anchor_valid;
	u16 seek_commit_value;
	u16 seek_anchor;
} GUI_Player_Context_t;

static int GUI_Player_PageBuild(Page_Struct_t *page);
static int GUI_Player_PageLoaded(Page_Struct_t *page, void *user_data);
static GUI_Player_Context_t *GUI_Player_GetContext(void);
static int GUI_Player_OpenAudio(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data);
static int GUI_Player_Back(GUI_FileManage_t *manager, void *user_data);
static void GUI_Player_CreateControls(GUI_Player_Context_t *context);
static lv_obj_t *GUI_Player_CreateButton(lv_obj_t *parent, const char *symbol, lv_coord_t x, lv_coord_t y, lv_event_cb_t cb, GUI_Player_Context_t *context);
static void GUI_Player_PlayEvent(lv_event_t *event);
static void GUI_Player_StopEvent(lv_event_t *event);
static void GUI_Player_VolumeEvent(lv_event_t *event);
static void GUI_Player_SeekEvent(lv_event_t *event);
static u8 GUI_Player_HandleKey(u32 key);
static void GUI_Player_PollTimer(lv_timer_t *timer);
static void GUI_Player_SeekCommitTimer(lv_timer_t *timer);
static int GUI_Player_EnsureTimer(void);
static void GUI_Player_UpdateView(GUI_Player_Context_t *context);
static void GUI_Player_UpdateSeekView(GUI_Player_Context_t *context, const Player_Info_t *info, u8 info_valid);
static void GUI_Player_UpdateSeekPreview(GUI_Player_Context_t *context, u16 position);
static void GUI_Player_RequestSeek(GUI_Player_Context_t *context);
static void GUI_Player_AdjustSeek(GUI_Player_Context_t *context, s16 delta);
static void GUI_Player_ScheduleSeek(GUI_Player_Context_t *context);
static u8 GUI_Player_SetSeekFromPointer(GUI_Player_Context_t *context);
static void GUI_Player_SetVolume(GUI_Player_Context_t *context, int volume);
static void GUI_Player_UpdateVolumeView(GUI_Player_Context_t *context, int volume);
static int GUI_Player_LimitVolume(int volume);
static const char *GUI_Player_StateText(Player_State_t state);
static void GUI_Player_ApplyTrackFont(lv_obj_t *label);
static void GUI_Player_FormatTime(char *dst, u32 dst_size, u32 seconds);
static void GUI_Player_SetLabelTextIfChanged(lv_obj_t *label, const char *text);
static u8 GUI_Player_CopyText(char *dst, u32 dst_size, const char *src);
static const char *GUI_Player_TrackText(GUI_Player_Context_t *context, char *buffer, u32 buffer_size);
static const char *GUI_Player_FileName(const char *path);

static GUI_Player_Context_t *s_player;
static const char * const s_player_extensions[] =
{
	".wav",
	".mp3"
};

int GUI_Player_Register(void)
{
	int code;

	if (GUI_Player_GetContext() == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	if (Page_Find(GUI_PAGE_MUSIC) != NULL)
	{
		return GUI_Player_EnsureTimer();
	}

	code = Page_Create(GUI_PAGE_MUSIC, "Music", GUI_Player_PageBuild, GUI_Player_PageLoaded);
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	return GUI_Player_EnsureTimer();
}

int GUI_Player_Show(void)
{
	int code;

	if (Page_Find(GUI_PAGE_MUSIC) == NULL)
	{
		code = GUI_Player_Register();
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	return Page_Show_Anim(GUI_PAGE_MUSIC, LV_SCR_LOAD_ANIM_MOVE_LEFT, GUI_M3_MOTION_PAGE, 0U);
}

static int GUI_Player_PageBuild(Page_Struct_t *page)
{
	GUI_Player_Context_t *context;
	GUI_FileManageConfig_t config;
	int code;

	if (page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}

	context = GUI_Player_GetContext();
	if (context == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	memset(context, 0, sizeof(*context));
	memset(&config, 0, sizeof(config));

	config.title = "Music";
	config.root_path = "0:/";
	config.empty_text = "No audio files";
	config.status_text = "Audio";
	config.extensions = s_player_extensions;
	config.extension_count = (u8)(sizeof(s_player_extensions) / sizeof(s_player_extensions[0]));
	config.accent_color = GUI_Theme_Color(GUI_THEME_COLOR_PRIMARY);
	config.hide_default_status = 1U;
	config.open = GUI_Player_OpenAudio;
	config.back = GUI_Player_Back;
	config.user_data = context;

	code = GUI_FileManage_Create(&context->file_manage, page, &config);
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	if (context->file_manage.list != NULL)
	{
		lv_obj_set_pos(context->file_manage.list, 10, GUI_PLAYER_LIST_Y);
		lv_obj_set_height(context->file_manage.list, GUI_PLAYER_LIST_H);
	}
	if (context->file_manage.status_label != NULL)
	{
		lv_obj_add_flag(context->file_manage.status_label, LV_OBJ_FLAG_HIDDEN);
	}

	context->volume = (u8)GUI_Player_LimitVolume(audio_card_get_volume());
	GUI_Player_CreateControls(context);
	GUI_Player_UpdateView(context);

	return SYSTEM_STATUS_OK;
}

static int GUI_Player_PageLoaded(Page_Struct_t *page, void *user_data)
{
	GUI_Player_Context_t *context = s_player;

	(void)page;
	(void)user_data;

	if (context == NULL)
	{
		return OPERATE_FAILED;
	}

	GUI_Player_UpdateView(context);
	return GUI_FileManage_PageLoaded(&context->file_manage);
}

static GUI_Player_Context_t *GUI_Player_GetContext(void)
{
	if (s_player == NULL)
	{
		s_player = (GUI_Player_Context_t *)sys_malloc(SRAMEX, sizeof(GUI_Player_Context_t));
		if (s_player != NULL)
		{
			memset(s_player, 0, sizeof(GUI_Player_Context_t));
		}
	}

	return s_player;
}

static int GUI_Player_OpenAudio(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data)
{
	GUI_Player_Context_t *context = (GUI_Player_Context_t *)user_data;
	int code;

	if (context == NULL || manager == NULL || item == NULL)
	{
		return OPERATE_FAILED;
	}

	if (PlayerIsBusy() != 0U)
	{
		GUI_Player_SetLabelTextIfChanged(context->state_label, "Stop current audio first");
		GUI_FileManage_SetStatus(manager, "Stop current audio first");
		return OPERATE_FAILED;
	}

	if (GUI_FileManage_BuildItemPath(manager, item, context->current_path, GUI_FILEMANAGE_PATH_SIZE) == 0U ||
	    GUI_Player_CopyText(context->current_name, GUI_FILEMANAGE_NAME_SIZE, item->name) == 0U)
	{
		GUI_Player_SetLabelTextIfChanged(context->state_label, "Path too long");
		GUI_FileManage_SetStatus(manager, "Path too long");
		return OPERATE_FAILED;
	}
	if (GUI_FileManage_OemToUtf8(context->current_name_utf8, sizeof(context->current_name_utf8), item->name) == 0U)
	{
		(void)GUI_Player_CopyText(context->current_name_utf8, sizeof(context->current_name_utf8), context->current_name);
	}

	GUI_Player_SetLabelTextIfChanged(context->state_label, "Starting audio");
	GUI_FileManage_SetStatus(manager, "Starting audio");
	code = PlayerStart((const TCHAR *)context->current_path);
	if (code != SYSTEM_STATUS_OK)
	{
		GUI_Player_SetLabelTextIfChanged(context->state_label, "Play failed");
		GUI_FileManage_SetStatus(manager, "Play failed");
		GUI_Player_UpdateView(context);
		return code;
	}
	(void)audio_card_chg_volume(context->volume);

	GUI_FileManage_SetStatus(manager, "Playing");
	GUI_Player_UpdateView(context);
	return SYSTEM_STATUS_OK;
}

static int GUI_Player_Back(GUI_FileManage_t *manager, void *user_data)
{
	(void)manager;
	(void)user_data;

	return Page_Back();
}

static void GUI_Player_CreateControls(GUI_Player_Context_t *context)
{
	if (context == NULL || context->file_manage.screen == NULL)
	{
		return;
	}

	context->info_panel = GUI_M3_CreateCard(context->file_manage.screen, GUI_M3_CARD_FILLED);
	lv_obj_set_size(context->info_panel, GUI_PLAYER_INFO_W, GUI_PLAYER_INFO_H);
	lv_obj_set_pos(context->info_panel, GUI_PLAYER_INFO_X, GUI_PLAYER_INFO_Y);
	/* The card keeps its own absolute child layout, so drop the MD3 card padding. */
	lv_obj_set_style_pad_all(context->info_panel, 0, 0);

	context->track_label = lv_label_create(context->info_panel);
	lv_label_set_long_mode(context->track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_obj_set_width(context->track_label, 280);
	lv_obj_set_pos(context->track_label, 10, 5);
	lv_obj_set_style_text_color(context->track_label, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE), 0);
	GUI_Player_ApplyTrackFont(context->track_label);

	context->state_label = lv_label_create(context->info_panel);
	lv_label_set_long_mode(context->state_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(context->state_label, 138);
	lv_obj_set_pos(context->state_label, 10, 28);
	lv_obj_set_style_text_font(context->state_label, GUI_M3_Font(GUI_M3_TYPE_LABEL), 0);
	lv_obj_set_style_text_color(context->state_label, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE_VARIANT), 0);

	context->time_label = lv_label_create(context->info_panel);
	lv_label_set_long_mode(context->time_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(context->time_label, 138);
	lv_obj_set_pos(context->time_label, 152, 28);
	lv_obj_set_style_text_align(context->time_label, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_style_text_font(context->time_label, GUI_M3_Font(GUI_M3_TYPE_LABEL), 0);
	lv_obj_set_style_text_color(context->time_label, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE_VARIANT), 0);

	context->seek_slider = lv_slider_create(context->info_panel);
	lv_obj_clear_flag(context->seek_slider, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(context->seek_slider, 280, 7);
	lv_obj_set_pos(context->seek_slider, 10, 46);
	lv_slider_set_range(context->seek_slider, 0, GUI_PLAYER_SEEK_MAX);
	GUI_M3_StyleSlider(context->seek_slider);
	lv_obj_add_event_cb(context->seek_slider, GUI_Player_SeekEvent, LV_EVENT_HIT_TEST, context);
	lv_obj_add_event_cb(context->seek_slider, GUI_Player_SeekEvent, LV_EVENT_PRESSED, context);
	lv_obj_add_event_cb(context->seek_slider, GUI_Player_SeekEvent, LV_EVENT_PRESSING, context);
	lv_obj_add_event_cb(context->seek_slider, GUI_Player_SeekEvent, LV_EVENT_RELEASED, context);
	lv_obj_add_event_cb(context->seek_slider, GUI_Player_SeekEvent, LV_EVENT_PRESS_LOST, context);
	lv_obj_add_event_cb(context->seek_slider, GUI_Player_SeekEvent, LV_EVENT_VALUE_CHANGED, context);
	lv_obj_add_event_cb(context->seek_slider, GUI_Player_SeekEvent, LV_EVENT_KEY, context);
	context->seek_commit_timer = lv_timer_create(GUI_Player_SeekCommitTimer,
	                                            GUI_PLAYER_SEEK_COMMIT_MS,
	                                            context);
	if (context->seek_commit_timer != NULL)
	{
		lv_timer_pause(context->seek_commit_timer);
	}

	context->play_btn = GUI_Player_CreateButton(context->file_manage.screen, GUI_ICON_PLAY, GUI_PLAYER_PLAY_X, GUI_PLAYER_CTRL_Y, GUI_Player_PlayEvent, context);
	/* Play is the primary transport action, so promote it to a filled icon button. */
	GUI_M3_StyleButton(context->play_btn, GUI_M3_BUTTON_ICON_FILLED);
	context->play_icon = (context->play_btn != NULL) ? lv_obj_get_child(context->play_btn, 0) : NULL;
	context->stop_btn = GUI_Player_CreateButton(context->file_manage.screen, GUI_ICON_STOP, GUI_PLAYER_STOP_X, GUI_PLAYER_CTRL_Y, GUI_Player_StopEvent, context);
	context->stop_icon = (context->stop_btn != NULL) ? lv_obj_get_child(context->stop_btn, 0) : NULL;

	context->volume_icon = GUI_M3_CreateIconLabel(context->file_manage.screen, GUI_ICON_VOLUME_UP,
	                                              GUI_THEME_COLOR_ON_SURFACE_VARIANT, 0U);
	lv_obj_set_pos(context->volume_icon, GUI_PLAYER_VOL_ICON_X, GUI_PLAYER_CTRL_Y + 6);

	context->volume_slider = lv_slider_create(context->file_manage.screen);
	lv_obj_clear_flag(context->volume_slider, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(context->volume_slider, GUI_PLAYER_VOL_SLIDER_W, 8);
	lv_obj_set_pos(context->volume_slider, GUI_PLAYER_VOL_SLIDER_X, GUI_PLAYER_CTRL_Y + 12);
	lv_slider_set_range(context->volume_slider, 0, GUI_PLAYER_VOLUME_MAX);
	GUI_M3_StyleSlider(context->volume_slider);
	lv_obj_add_event_cb(context->volume_slider, GUI_Player_VolumeEvent, LV_EVENT_VALUE_CHANGED, context);
	lv_obj_add_event_cb(context->volume_slider, GUI_Player_VolumeEvent, LV_EVENT_KEY, context);

	context->volume_label = lv_label_create(context->file_manage.screen);
	lv_label_set_long_mode(context->volume_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(context->volume_label, GUI_PLAYER_VOL_LABEL_W);
	lv_obj_set_pos(context->volume_label, GUI_PLAYER_VOL_LABEL_X, GUI_PLAYER_CTRL_Y + 10);
	lv_obj_set_style_text_align(context->volume_label, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_style_text_font(context->volume_label, GUI_M3_Font(GUI_M3_TYPE_LABEL), 0);
	lv_obj_set_style_text_color(context->volume_label, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE), 0);
	GUI_Player_UpdateVolumeView(context, context->volume);
}

/* MD3 icon button: pill shaped, tonal container, Material Symbols glyph.  The
 * icon label stays child 0 so the caller can still swap the glyph at runtime. */
static lv_obj_t *GUI_Player_CreateButton(lv_obj_t *parent, const char *symbol, lv_coord_t x, lv_coord_t y, lv_event_cb_t cb, GUI_Player_Context_t *context)
{
	lv_obj_t *btn;

	btn = GUI_M3_CreateIconButton(parent, symbol, GUI_M3_BUTTON_ICON_TONAL);
	if (btn == NULL)
	{
		return NULL;
	}

	lv_obj_set_size(btn, GUI_PLAYER_CTRL_SIZE, GUI_PLAYER_CTRL_SIZE);
	lv_obj_set_pos(btn, x, y);
	lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, context);
	lv_obj_add_event_cb(btn, cb, LV_EVENT_KEY, context);

	return btn;
}

static void GUI_Player_PlayEvent(lv_event_t *event)
{
	GUI_Player_Context_t *context = (GUI_Player_Context_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	Player_State_t state;

	if (context == NULL)
	{
		return;
	}

	if (code == LV_EVENT_KEY)
	{
		if (lv_event_get_key(event) == LV_KEY_ESC)
		{
			(void)Page_Back();
		}
		else
		{
			(void)GUI_Player_HandleKey(lv_event_get_key(event));
		}
		return;
	}

	if (code != LV_EVENT_CLICKED)
	{
		return;
	}

	state = PlayerGetState();
	if (state == PLAYER_STATE_PAUSED)
	{
		(void)PlayerResume();
	}
	else if (state == PLAYER_STATE_PLAYING || state == PLAYER_STATE_STARTING)
	{
		(void)PlayerPause();
	}
	else if (context->current_path[0] != '\0' && PlayerIsBusy() == 0U)
	{
		if (PlayerStart((const TCHAR *)context->current_path) == SYSTEM_STATUS_OK)
		{
			(void)audio_card_chg_volume(context->volume);
		}
	}

	GUI_Player_UpdateView(context);
}

static void GUI_Player_VolumeEvent(lv_event_t *event)
{
	GUI_Player_Context_t *context = (GUI_Player_Context_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;

	if (context == NULL)
	{
		return;
	}

	if (code == LV_EVENT_VALUE_CHANGED)
	{
		GUI_Player_SetVolume(context, (int)lv_slider_get_value(context->volume_slider));
		return;
	}

	if (code != LV_EVENT_KEY)
	{
		return;
	}

	key = lv_event_get_key(event);
	if (key == LV_KEY_ESC)
	{
		(void)Page_Back();
	}
	else if (key == LV_KEY_UP || key == LV_KEY_PREV)
	{
		(void)GUI_Player_HandleKey(key);
	}
	else if (key == LV_KEY_DOWN || key == LV_KEY_NEXT)
	{
		(void)GUI_Player_HandleKey(key);
	}
}

static void GUI_Player_SeekEvent(lv_event_t *event)
{
	GUI_Player_Context_t *context = (GUI_Player_Context_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;

	if (context == NULL)
	{
		return;
	}

	if (code == LV_EVENT_HIT_TEST)
	{
		lv_hit_test_info_t *info = (lv_hit_test_info_t *)lv_event_get_param(event);
		if (info != NULL)
		{
			info->res = true;
		}
		return;
	}

	if (code == LV_EVENT_PRESSED)
	{
		if (GUI_Player_SetSeekFromPointer(context) != 0U)
		{
			context->seek_commit_pending = 0U;
			if (context->seek_commit_timer != NULL)
			{
				lv_timer_pause(context->seek_commit_timer);
			}
			context->seek_pressed = 1U;
		}
		return;
	}

	if (code == LV_EVENT_PRESSING)
	{
		(void)GUI_Player_SetSeekFromPointer(context);
		return;
	}

	if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
	{
		if (context->seek_pressed != 0U)
		{
			context->seek_pressed = 0U;
			GUI_Player_RequestSeek(context);
		}
		return;
	}

	if (code == LV_EVENT_VALUE_CHANGED)
	{
		if (context->seek_updating == 0U)
		{
			GUI_Player_UpdateSeekPreview(context, (u16)lv_slider_get_value(context->seek_slider));
		}
		return;
	}

	if (code != LV_EVENT_KEY)
	{
		return;
	}

	key = lv_event_get_key(event);
	if (key == LV_KEY_ESC)
	{
		(void)Page_Back();
	}
	else if (key == LV_KEY_LEFT)
	{
		GUI_Player_AdjustSeek(context, -(s16)GUI_PLAYER_SEEK_KEY_STEP);
	}
	else if (key == LV_KEY_RIGHT)
	{
		GUI_Player_AdjustSeek(context, (s16)GUI_PLAYER_SEEK_KEY_STEP);
	}
	else if (key == LV_KEY_UP || key == LV_KEY_PREV ||
	         key == LV_KEY_DOWN || key == LV_KEY_NEXT)
	{
		(void)GUI_Player_HandleKey(key);
	}
	else if (key == LV_KEY_ENTER)
	{
		GUI_Player_RequestSeek(context);
	}
}

static void GUI_Player_StopEvent(lv_event_t *event)
{
	GUI_Player_Context_t *context = (GUI_Player_Context_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);

	if (context == NULL)
	{
		return;
	}

	if (code == LV_EVENT_KEY)
	{
		if (lv_event_get_key(event) == LV_KEY_ESC)
		{
			(void)Page_Back();
		}
		else
		{
			(void)GUI_Player_HandleKey(lv_event_get_key(event));
		}
		return;
	}

	if (code != LV_EVENT_CLICKED)
	{
		return;
	}

	(void)PlayerStop();
	GUI_Player_UpdateView(context);
}

static u8 GUI_Player_HandleKey(u32 key)
{
	lv_group_t *group;

	group = lv_group_get_default();
	if (group == NULL)
	{
		return 0U;
	}

	if (key == LV_KEY_LEFT || key == LV_KEY_UP || key == LV_KEY_PREV)
	{
		lv_group_focus_prev(group);
		return 1U;
	}

	if (key == LV_KEY_RIGHT || key == LV_KEY_DOWN || key == LV_KEY_NEXT)
	{
		lv_group_focus_next(group);
		return 1U;
	}

	return 0U;
}

static void GUI_Player_PollTimer(lv_timer_t *timer)
{
	GUI_Player_Context_t *context = (GUI_Player_Context_t *)timer->user_data;

	PlayerCleanup();
	if (context == NULL)
	{
		context = s_player;
	}

	if (context == NULL || context->file_manage.screen == NULL)
	{
		return;
	}

	GUI_Player_UpdateView(context);
}

static void GUI_Player_SeekCommitTimer(lv_timer_t *timer)
{
	GUI_Player_Context_t *context;

	if (timer == NULL)
	{
		return;
	}

	context = (GUI_Player_Context_t *)timer->user_data;
	if ((KEY_GetState() & (KEY_GAME_LEFT | KEY_GAME_RIGHT)) != 0U)
	{
		/* LVGL does not emit RELEASED for keypad direction keys.  Keep the
		 * debounce timer alive while a physical seek key is still held. */
		lv_timer_reset(timer);
		return;
	}
	lv_timer_pause(timer);
	GUI_Player_RequestSeek(context);
}

static int GUI_Player_EnsureTimer(void)
{
	GUI_Player_Context_t *context = GUI_Player_GetContext();

	if (context == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	if (context->poll_timer != NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	context->poll_timer = lv_timer_create(GUI_Player_PollTimer, GUI_PLAYER_TIMER_MS, context);
	if (context->poll_timer == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	return SYSTEM_STATUS_OK;
}

static void GUI_Player_UpdateView(GUI_Player_Context_t *context)
{
	Player_State_t state;
	Player_Info_t info;
	u8 info_valid;
	const char *track;
	char track_buffer[GUI_PLAYER_NAME_UTF8_SIZE];
	char text[64];

	if (context == NULL)
	{
		return;
	}

	state = PlayerGetState();
	memset(&info, 0, sizeof(info));
	info_valid = (PlayerGetInfo(&info) == SYSTEM_STATUS_OK && info.sample_rate != 0U) ? 1U : 0U;
	track = GUI_Player_TrackText(context, track_buffer, sizeof(track_buffer));

	if (context->track_label != NULL)
	{
		GUI_Player_SetLabelTextIfChanged(context->track_label, (track != NULL && track[0] != '\0') ? track : "No track");
	}

	if (context->state_label != NULL)
	{
		if (info_valid != 0U)
		{
			(void)snprintf(text, sizeof(text), "%s  %luHz %dch",
			               GUI_Player_StateText(state),
			               (unsigned long)info.sample_rate,
			               info.channels);
			GUI_Player_SetLabelTextIfChanged(context->state_label, text);
		}
		else
		{
			GUI_Player_SetLabelTextIfChanged(context->state_label, GUI_Player_StateText(state));
		}
	}

	if (context->play_icon != NULL)
	{
		lv_label_set_text(context->play_icon, (state == PLAYER_STATE_PLAYING || state == PLAYER_STATE_STARTING) ? GUI_ICON_PAUSE : GUI_ICON_PLAY);
	}

	if (context->play_btn != NULL)
	{
		if (state == PLAYER_STATE_STOPPING || (context->current_path[0] == '\0' && PlayerIsBusy() == 0U))
		{
			lv_obj_add_state(context->play_btn, LV_STATE_DISABLED);
		}
		else
		{
			lv_obj_clear_state(context->play_btn, LV_STATE_DISABLED);
		}
	}

	if (context->stop_btn != NULL)
	{
		if (PlayerIsBusy() != 0U)
		{
			lv_obj_clear_state(context->stop_btn, LV_STATE_DISABLED);
		}
		else
		{
			lv_obj_add_state(context->stop_btn, LV_STATE_DISABLED);
		}
	}

	GUI_Player_UpdateSeekView(context, &info, info_valid);
	GUI_Player_UpdateVolumeView(context, audio_card_get_volume());
}

static void GUI_Player_UpdateSeekView(GUI_Player_Context_t *context, const Player_Info_t *info, u8 info_valid)
{
	u32 duration = 0U;
	u32 position = 0U;
	u32 value = 0U;
	u8 can_seek = 0U;

	if (context == NULL)
	{
		return;
	}

	if (info_valid != 0U && info != NULL)
	{
		duration = info->duration_seconds;
	}
	if (duration != 0U)
	{
		can_seek = (PlayerCanSeek() != 0U && PlayerIsBusy() != 0U) ? 1U : 0U;
		if (can_seek == 0U)
		{
			context->seek_commit_pending = 0U;
			context->seek_anchor_valid = 0U;
			if (context->seek_commit_timer != NULL)
			{
				lv_timer_pause(context->seek_commit_timer);
			}
		}
		else if (context->seek_commit_pending != 0U)
		{
			value = (u32)context->seek_commit_value;
			position = (duration * value) / GUI_PLAYER_SEEK_MAX;
		}
		else if (PlayerSeekIsPending() != 0U)
		{
			value = (u32)PlayerGetSeekTargetPermille();
			if (value > GUI_PLAYER_SEEK_MAX)
			{
				value = GUI_PLAYER_SEEK_MAX;
			}
			position = (duration * value) / GUI_PLAYER_SEEK_MAX;
		}
		else
		{
			position = PlayerGetPositionSeconds();
			if (position > duration)
			{
				position = duration;
			}
			value = (position * GUI_PLAYER_SEEK_MAX) / duration;
			if (value > GUI_PLAYER_SEEK_MAX)
			{
				value = GUI_PLAYER_SEEK_MAX;
			}

			/* tell_seconds() intentionally uses whole seconds.  Keep the exact
			 * requested permille until playback has caught up so that a forward
			 * seek is not rounded backwards by the 250 ms UI refresh. */
			if (context->seek_anchor_valid != 0U)
			{
				if (value < (u32)context->seek_anchor)
				{
					value = (u32)context->seek_anchor;
					position = (duration * value) / GUI_PLAYER_SEEK_MAX;
				}
				else
				{
					context->seek_anchor_valid = 0U;
				}
			}
		}
	}
	else
	{
		context->seek_anchor_valid = 0U;
	}

	if (context->seek_slider != NULL)
	{
		if (can_seek != 0U)
		{
			lv_obj_clear_state(context->seek_slider, LV_STATE_DISABLED);
		}
		else
		{
			lv_obj_add_state(context->seek_slider, LV_STATE_DISABLED);
		}

		if (context->seek_pressed == 0U && lv_slider_get_value(context->seek_slider) != (int)value)
		{
			context->seek_updating = 1U;
			lv_slider_set_value(context->seek_slider, (int)value, LV_ANIM_OFF);
			context->seek_updating = 0U;
		}
	}

	if (context->time_label != NULL)
	{
		if (duration != 0U)
		{
			char pos_text[16];
			char dur_text[16];
			char time_text[40];

			GUI_Player_FormatTime(pos_text, sizeof(pos_text), position);
			GUI_Player_FormatTime(dur_text, sizeof(dur_text), duration);
			(void)snprintf(time_text, sizeof(time_text), "%s / %s", pos_text, dur_text);
			GUI_Player_SetLabelTextIfChanged(context->time_label, time_text);
		}
		else
		{
			GUI_Player_SetLabelTextIfChanged(context->time_label, "--:-- / --:--");
		}
	}
}

static void GUI_Player_UpdateSeekPreview(GUI_Player_Context_t *context, u16 position)
{
	Player_Info_t info;
	u32 seconds;
	char pos_text[16];
	char dur_text[16];
	char text[40];

	if (context == NULL || context->time_label == NULL)
	{
		return;
	}

	if (PlayerGetInfo(&info) != SYSTEM_STATUS_OK || info.duration_seconds == 0U)
	{
		GUI_Player_SetLabelTextIfChanged(context->time_label, "--:-- / --:--");
		return;
	}
	if (position > GUI_PLAYER_SEEK_MAX)
	{
		position = GUI_PLAYER_SEEK_MAX;
	}

	seconds = (info.duration_seconds * (u32)position) / GUI_PLAYER_SEEK_MAX;
	GUI_Player_FormatTime(pos_text, sizeof(pos_text), seconds);
	GUI_Player_FormatTime(dur_text, sizeof(dur_text), info.duration_seconds);
	(void)snprintf(text, sizeof(text), "%s / %s", pos_text, dur_text);
	GUI_Player_SetLabelTextIfChanged(context->time_label, text);
}

static void GUI_Player_RequestSeek(GUI_Player_Context_t *context)
{
	int value;

	if (context == NULL)
	{
		return;
	}
	if (context->seek_commit_timer != NULL)
	{
		lv_timer_pause(context->seek_commit_timer);
	}
	if (context->seek_slider == NULL ||
	    PlayerCanSeek() == 0U || PlayerIsBusy() == 0U)
	{
		context->seek_commit_pending = 0U;
		context->seek_anchor_valid = 0U;
		return;
	}

	value = (context->seek_commit_pending != 0U) ?
	        (int)context->seek_commit_value :
	        lv_slider_get_value(context->seek_slider);
	context->seek_commit_pending = 0U;
	if (value < 0)
	{
		value = 0;
	}
	else if (value > GUI_PLAYER_SEEK_MAX)
	{
		value = GUI_PLAYER_SEEK_MAX;
	}

	GUI_Player_UpdateSeekPreview(context, (u16)value);
	context->seek_anchor = (u16)value;
	context->seek_anchor_valid = 1U;
	if (PlayerSeekPermille((u16)value) != SYSTEM_STATUS_OK)
	{
		context->seek_anchor_valid = 0U;
		GUI_Player_SetLabelTextIfChanged(context->state_label, "Seek failed");
	}
}

static void GUI_Player_AdjustSeek(GUI_Player_Context_t *context, s16 delta)
{
	int value;

	if (context == NULL || context->seek_slider == NULL)
	{
		return;
	}

	value = lv_slider_get_value(context->seek_slider) + (int)delta;
	if (value < 0)
	{
		value = 0;
	}
	else if (value > GUI_PLAYER_SEEK_MAX)
	{
		value = GUI_PLAYER_SEEK_MAX;
	}

	context->seek_updating = 1U;
	lv_slider_set_value(context->seek_slider, value, LV_ANIM_OFF);
	context->seek_updating = 0U;
	GUI_Player_UpdateSeekPreview(context, (u16)value);
	context->seek_commit_value = (u16)value;
	context->seek_commit_pending = 1U;
	GUI_Player_ScheduleSeek(context);
}

static void GUI_Player_ScheduleSeek(GUI_Player_Context_t *context)
{
	if (context == NULL)
	{
		return;
	}

	/* Key repeat is much faster than an MP3 decoder/DMA restart.  Resetting a
	 * one-shot timer coalesces a held key into one seek to the latest target. */
	if (context->seek_commit_timer == NULL)
	{
		GUI_Player_RequestSeek(context);
		return;
	}

	lv_timer_reset(context->seek_commit_timer);
	lv_timer_resume(context->seek_commit_timer);
}

static u8 GUI_Player_SetSeekFromPointer(GUI_Player_Context_t *context)
{
	lv_indev_t *indev;
	lv_point_t point;
	lv_area_t area;
	lv_coord_t width;
	lv_coord_t relative;
	lv_base_dir_t base_dir;
	int value;

	if (context == NULL || context->seek_slider == NULL)
	{
		return 0U;
	}

	indev = lv_indev_get_act();
	if (indev == NULL || lv_indev_get_type(indev) != LV_INDEV_TYPE_POINTER)
	{
		return 0U;
	}

	lv_indev_get_point(indev, &point);
	lv_obj_get_coords(context->seek_slider, &area);
	width = area.x2 - area.x1;
	if (width <= 0)
	{
		return 0U;
	}

	base_dir = lv_obj_get_style_base_dir(context->seek_slider, LV_PART_MAIN);
	if (point.x <= area.x1)
	{
		relative = 0;
	}
	else if (point.x >= area.x2)
	{
		relative = width;
	}
	else
	{
		relative = point.x - area.x1;
	}

	if (base_dir == LV_BASE_DIR_RTL)
	{
		relative = width - relative;
	}

	value = ((int)relative * GUI_PLAYER_SEEK_MAX) / (int)width;
	if (value < 0)
	{
		value = 0;
	}
	else if (value > GUI_PLAYER_SEEK_MAX)
	{
		value = GUI_PLAYER_SEEK_MAX;
	}

	context->seek_updating = 1U;
	lv_slider_set_value(context->seek_slider, value, LV_ANIM_OFF);
	context->seek_updating = 0U;
	GUI_Player_UpdateSeekPreview(context, (u16)value);
	return 1U;
}

static void GUI_Player_SetVolume(GUI_Player_Context_t *context, int volume)
{
	if (context == NULL)
	{
		return;
	}

	volume = GUI_Player_LimitVolume(volume);
	context->volume = (u8)volume;
	(void)audio_card_chg_volume(volume);
	GUI_Player_UpdateVolumeView(context, volume);
}

static void GUI_Player_UpdateVolumeView(GUI_Player_Context_t *context, int volume)
{
	char text[8];

	if (context == NULL)
	{
		return;
	}

	volume = GUI_Player_LimitVolume(volume);
	context->volume = (u8)volume;

	if (context->volume_slider != NULL && lv_slider_get_value(context->volume_slider) != volume)
	{
		lv_slider_set_value(context->volume_slider, volume, LV_ANIM_OFF);
	}

	if (context->volume_icon != NULL)
	{
		if (volume == 0)
		{
			lv_label_set_text(context->volume_icon, GUI_ICON_VOLUME_OFF);
		}
		else
		{
			lv_label_set_text(context->volume_icon, GUI_ICON_VOLUME_UP);
		}
	}

	if (context->volume_label != NULL)
	{
		(void)snprintf(text, sizeof(text), "%d%%", volume);
		lv_label_set_text(context->volume_label, text);
	}
}

static int GUI_Player_LimitVolume(int volume)
{
	if (volume < 0)
	{
		return 0;
	}

	if (volume > GUI_PLAYER_VOLUME_MAX)
	{
		return GUI_PLAYER_VOLUME_MAX;
	}

	return volume;
}

static const char *GUI_Player_StateText(Player_State_t state)
{
	switch (state)
	{
		case PLAYER_STATE_STARTING:
			return "Starting";
		case PLAYER_STATE_PLAYING:
			return "Playing";
		case PLAYER_STATE_PAUSED:
			return "Paused";
		case PLAYER_STATE_STOPPING:
			return "Stopping";
		case PLAYER_STATE_ERROR:
			return "Error";
		case PLAYER_STATE_IDLE:
		default:
			return "Stopped";
	}
}

static void GUI_Player_ApplyTrackFont(lv_obj_t *label)
{
	const lv_font_t *font;

	if (label == NULL)
	{
		return;
	}

	font = GUI_Font_GetXbfFont();
	lv_obj_set_style_text_font(label, (font != NULL) ? font : LV_FONT_DEFAULT, 0);
}

static void GUI_Player_FormatTime(char *dst, u32 dst_size, u32 seconds)
{
	u32 hours;
	u32 minutes;

	if (dst == NULL || dst_size == 0U)
	{
		return;
	}

	hours = seconds / 3600U;
	seconds %= 3600U;
	minutes = seconds / 60U;
	seconds %= 60U;

	if (hours != 0U)
	{
		(void)snprintf(dst, dst_size, "%lu:%02lu:%02lu",
		               (unsigned long)hours,
		               (unsigned long)minutes,
		               (unsigned long)seconds);
	}
	else
	{
		(void)snprintf(dst, dst_size, "%lu:%02lu",
		               (unsigned long)minutes,
		               (unsigned long)seconds);
	}
}

static void GUI_Player_SetLabelTextIfChanged(lv_obj_t *label, const char *text)
{
	const char *old_text;

	if (label == NULL)
	{
		return;
	}

	if (text == NULL)
	{
		text = "";
	}

	old_text = lv_label_get_text(label);
	if (old_text == NULL || strcmp(old_text, text) != 0)
	{
		lv_label_set_text(label, text);
	}
}

static u8 GUI_Player_CopyText(char *dst, u32 dst_size, const char *src)
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

static const char *GUI_Player_TrackText(GUI_Player_Context_t *context, char *buffer, u32 buffer_size)
{
	const char *name;

	if (context == NULL)
	{
		return "";
	}

	if (context->current_name_utf8[0] != '\0')
	{
		return context->current_name_utf8;
	}

	name = GUI_Player_FileName((const char *)PlayerGetCurrentPath());
	if (name == NULL || name[0] == '\0')
	{
		return "";
	}

	if (buffer != NULL && buffer_size != 0U &&
	    GUI_FileManage_OemToUtf8(buffer, buffer_size, name) != 0U)
	{
		return buffer;
	}

	return name;
}

static const char *GUI_Player_FileName(const char *path)
{
	const char *slash;
	const char *backslash;

	if (path == NULL)
	{
		return "";
	}

	slash = strrchr(path, '/');
	backslash = strrchr(path, '\\');
	if (backslash != NULL && (slash == NULL || backslash > slash))
	{
		slash = backslash;
	}

	return (slash != NULL) ? (slash + 1) : path;
}
