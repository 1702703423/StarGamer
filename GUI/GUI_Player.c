#include "GUI_Player.h"
#include "GUI_FileManage.h"
#include "Player.h"
#include "i2s.h"
#include "System_Status.h"
#include "Malloc.h"

#include <stdio.h>
#include <string.h>

#define GUI_PLAYER_ACCENT              0x1E88E5U
#define GUI_PLAYER_LIST_H              82
#define GUI_PLAYER_BAR_X               10
#define GUI_PLAYER_BAR_Y               148
#define GUI_PLAYER_BAR_W               300
#define GUI_PLAYER_BAR_H               64
#define GUI_PLAYER_CTRL_SIZE           34
#define GUI_PLAYER_VOLUME_MAX          100
#define GUI_PLAYER_STATUS_Y            218
#define GUI_PLAYER_TIMER_MS            250U

typedef struct
{
	GUI_FileManage_t file_manage;
	char current_path[GUI_FILEMANAGE_PATH_SIZE];
	char current_name[GUI_FILEMANAGE_NAME_SIZE];
	lv_obj_t *bar;
	lv_obj_t *track_label;
	lv_obj_t *state_label;
	lv_obj_t *play_btn;
	lv_obj_t *play_icon;
	lv_obj_t *stop_btn;
	lv_obj_t *stop_icon;
	lv_obj_t *volume_icon;
	lv_obj_t *volume_slider;
	lv_obj_t *volume_label;
	lv_timer_t *poll_timer;
	Player_State_t last_state;
	u8 volume;
} GUI_Player_Context_t;

static int GUI_Player_PageBuild(Page_Struct_t *page);
static int GUI_Player_PageLoaded(Page_Struct_t *page, void *user_data);
static GUI_Player_Context_t *GUI_Player_GetContext(void);
static int GUI_Player_OpenAudio(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data);
static int GUI_Player_Back(GUI_FileManage_t *manager, void *user_data);
static void GUI_Player_CreateControls(GUI_Player_Context_t *context);
static lv_obj_t *GUI_Player_CreateButton(lv_obj_t *parent, const char *symbol, lv_coord_t x, lv_event_cb_t cb, GUI_Player_Context_t *context);
static void GUI_Player_PlayEvent(lv_event_t *event);
static void GUI_Player_StopEvent(lv_event_t *event);
static void GUI_Player_VolumeEvent(lv_event_t *event);
static u8 GUI_Player_HandleKey(u32 key);
static void GUI_Player_PollTimer(lv_timer_t *timer);
static int GUI_Player_EnsureTimer(void);
static void GUI_Player_UpdateView(GUI_Player_Context_t *context);
static void GUI_Player_SetVolume(GUI_Player_Context_t *context, int volume);
static void GUI_Player_UpdateVolumeView(GUI_Player_Context_t *context, int volume);
static int GUI_Player_LimitVolume(int volume);
static const char *GUI_Player_StateText(Player_State_t state);
static void GUI_Player_SetLabelTextIfChanged(lv_obj_t *label, const char *text);
static u8 GUI_Player_CopyText(char *dst, u32 dst_size, const char *src);
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

	return Page_Show_Anim(GUI_PAGE_MUSIC, LV_SCR_LOAD_ANIM_MOVE_LEFT, 120U, 0U);
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
	config.accent_color = GUI_PLAYER_ACCENT;
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
		lv_obj_set_height(context->file_manage.list, GUI_PLAYER_LIST_H);
	}
	if (context->file_manage.status_label != NULL)
	{
		lv_obj_set_pos(context->file_manage.status_label, 12, GUI_PLAYER_STATUS_Y);
	}

	context->last_state = PLAYER_STATE_IDLE;
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
		GUI_FileManage_SetStatus(manager, "Stop current audio first");
		return OPERATE_FAILED;
	}

	if (GUI_FileManage_BuildItemPath(manager, item, context->current_path, GUI_FILEMANAGE_PATH_SIZE) == 0U ||
	    GUI_Player_CopyText(context->current_name, GUI_FILEMANAGE_NAME_SIZE, item->name) == 0U)
	{
		GUI_FileManage_SetStatus(manager, "Path too long");
		return OPERATE_FAILED;
	}

	GUI_FileManage_SetStatus(manager, "Starting audio");
	code = PlayerStart((const TCHAR *)context->current_path);
	if (code != SYSTEM_STATUS_OK)
	{
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

	context->bar = lv_obj_create(context->file_manage.screen);
	lv_obj_clear_flag(context->bar, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(context->bar, GUI_PLAYER_BAR_W, GUI_PLAYER_BAR_H);
	lv_obj_set_pos(context->bar, GUI_PLAYER_BAR_X, GUI_PLAYER_BAR_Y);
	lv_obj_set_style_radius(context->bar, 6, 0);
	lv_obj_set_style_border_width(context->bar, 1, 0);
	lv_obj_set_style_border_color(context->bar, lv_color_hex(0x304150U), 0);
	lv_obj_set_style_bg_color(context->bar, lv_color_hex(0x151D25U), 0);
	lv_obj_set_style_bg_opa(context->bar, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(context->bar, 0, 0);

	context->track_label = lv_label_create(context->bar);
	lv_label_set_long_mode(context->track_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_obj_set_width(context->track_label, 180);
	lv_obj_set_pos(context->track_label, 10, 7);
	lv_obj_set_style_text_color(context->track_label, lv_color_hex(0xF5F7FAU), 0);

	context->state_label = lv_label_create(context->bar);
	lv_label_set_long_mode(context->state_label, LV_LABEL_LONG_SCROLL_CIRCULAR);
	lv_obj_set_width(context->state_label, 180);
	lv_obj_set_pos(context->state_label, 10, 27);
	lv_obj_set_style_text_color(context->state_label, lv_color_hex(0x95A3B0U), 0);

	context->play_btn = GUI_Player_CreateButton(context->bar, LV_SYMBOL_PLAY, 210, GUI_Player_PlayEvent, context);
	context->play_icon = lv_obj_get_child(context->play_btn, 0);
	context->stop_btn = GUI_Player_CreateButton(context->bar, LV_SYMBOL_STOP, 252, GUI_Player_StopEvent, context);
	context->stop_icon = lv_obj_get_child(context->stop_btn, 0);

	context->volume_icon = lv_label_create(context->bar);
	lv_label_set_text(context->volume_icon, LV_SYMBOL_VOLUME_MID);
	lv_obj_set_pos(context->volume_icon, 12, 42);
	lv_obj_set_style_text_color(context->volume_icon, lv_color_hex(0xAEB7C2U), 0);

	context->volume_slider = lv_slider_create(context->bar);
	lv_obj_clear_flag(context->volume_slider, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(context->volume_slider, 176, 8);
	lv_obj_set_pos(context->volume_slider, 42, 49);
	lv_slider_set_range(context->volume_slider, 0, GUI_PLAYER_VOLUME_MAX);
	lv_obj_set_style_radius(context->volume_slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
	lv_obj_set_style_radius(context->volume_slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(context->volume_slider, lv_color_hex(0x2B3642U), LV_PART_MAIN);
	lv_obj_set_style_bg_color(context->volume_slider, lv_color_hex(GUI_PLAYER_ACCENT), LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(context->volume_slider, lv_color_hex(0xF5F7FAU), LV_PART_KNOB);
	lv_obj_set_style_width(context->volume_slider, 14, LV_PART_KNOB);
	lv_obj_set_style_height(context->volume_slider, 14, LV_PART_KNOB);
	lv_obj_set_style_outline_width(context->volume_slider, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(context->volume_slider, 5, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(context->volume_slider, lv_color_hex(GUI_PLAYER_ACCENT), LV_STATE_FOCUSED);
	lv_obj_add_event_cb(context->volume_slider, GUI_Player_VolumeEvent, LV_EVENT_VALUE_CHANGED, context);
	lv_obj_add_event_cb(context->volume_slider, GUI_Player_VolumeEvent, LV_EVENT_KEY, context);

	context->volume_label = lv_label_create(context->bar);
	lv_label_set_long_mode(context->volume_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(context->volume_label, 56);
	lv_obj_set_pos(context->volume_label, 232, 41);
	lv_obj_set_style_text_align(context->volume_label, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_style_text_color(context->volume_label, lv_color_hex(0xF5F7FAU), 0);
	GUI_Player_UpdateVolumeView(context, context->volume);
}

static lv_obj_t *GUI_Player_CreateButton(lv_obj_t *parent, const char *symbol, lv_coord_t x, lv_event_cb_t cb, GUI_Player_Context_t *context)
{
	lv_obj_t *btn;
	lv_obj_t *icon;

	btn = lv_btn_create(parent);
	lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(btn, GUI_PLAYER_CTRL_SIZE, GUI_PLAYER_CTRL_SIZE);
	lv_obj_set_pos(btn, x, 7);
	lv_obj_set_style_radius(btn, 8, 0);
	lv_obj_set_style_bg_color(btn, lv_color_hex(0x243140U), 0);
	lv_obj_set_style_bg_color(btn, lv_color_hex(0x33485EU), LV_STATE_PRESSED);
	lv_obj_set_style_bg_color(btn, lv_color_hex(0x26313AU), LV_STATE_DISABLED);
	lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(btn, 0, 0);
	lv_obj_set_style_border_width(btn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(btn, lv_color_hex(GUI_PLAYER_ACCENT), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_width(btn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(btn, 1, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(btn, lv_color_hex(GUI_PLAYER_ACCENT), LV_STATE_FOCUSED);
	lv_obj_set_style_shadow_width(btn, 0, 0);
	lv_obj_set_style_pad_all(btn, 0, 0);
	lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, context);
	lv_obj_add_event_cb(btn, cb, LV_EVENT_KEY, context);

	icon = lv_label_create(btn);
	lv_label_set_text(icon, symbol);
	lv_obj_set_style_text_color(icon, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_center(icon);

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
	const char *track;
	char text[64];

	if (context == NULL)
	{
		return;
	}

	state = PlayerGetState();
	track = (context->current_name[0] != '\0') ? context->current_name : GUI_Player_FileName((const char *)PlayerGetCurrentPath());

	if (context->track_label != NULL)
	{
		GUI_Player_SetLabelTextIfChanged(context->track_label, (track != NULL && track[0] != '\0') ? track : "No track");
	}

	if (context->state_label != NULL)
	{
		if (PlayerGetInfo(&info) == SYSTEM_STATUS_OK && info.sample_rate != 0U)
		{
			(void)snprintf(text, sizeof(text), "%s  %luHz %dch %dbit",
			               GUI_Player_StateText(state),
			               (unsigned long)info.sample_rate,
			               info.channels,
			               info.bits_per_sample);
			GUI_Player_SetLabelTextIfChanged(context->state_label, text);
		}
		else
		{
			GUI_Player_SetLabelTextIfChanged(context->state_label, GUI_Player_StateText(state));
		}
	}

	if (context->play_icon != NULL)
	{
		lv_label_set_text(context->play_icon, (state == PLAYER_STATE_PLAYING || state == PLAYER_STATE_STARTING) ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
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

	GUI_Player_UpdateVolumeView(context, audio_card_get_volume());

	if (context->file_manage.status_label != NULL && state != context->last_state)
	{
		GUI_FileManage_SetStatus(&context->file_manage, GUI_Player_StateText(state));
		context->last_state = state;
	}
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
			lv_label_set_text(context->volume_icon, LV_SYMBOL_MUTE);
		}
		else if (volume >= 75)
		{
			lv_label_set_text(context->volume_icon, LV_SYMBOL_VOLUME_MAX);
		}
		else
		{
			lv_label_set_text(context->volume_icon, LV_SYMBOL_VOLUME_MID);
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
