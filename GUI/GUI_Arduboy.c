#include "GUI_Arduboy.h"

#include "Arduboy_Games.h"
#include "Arduboy_Runtime.h"
#include "System_Status.h"

#include <stdio.h>
#include <string.h>

#define GUI_ARDUBOY_ACCENT             0x43A047U
#define GUI_ARDUBOY_MAX_GAMES          8U
#define GUI_ARDUBOY_LIST_X             10
#define GUI_ARDUBOY_LIST_Y             48
#define GUI_ARDUBOY_LIST_W             300
#define GUI_ARDUBOY_LIST_H             150
#define GUI_ARDUBOY_ROW_H              42
#define GUI_ARDUBOY_STATUS_Y           211
#define GUI_ARDUBOY_POLL_MS            100U

typedef struct GUI_ArduboyContext
{
	lv_obj_t *screen;
	lv_obj_t *back_button;
	lv_obj_t *list;
	lv_obj_t *status_label;
	lv_obj_t *game_buttons[GUI_ARDUBOY_MAX_GAMES];
	u8 game_indices[GUI_ARDUBOY_MAX_GAMES];
	u8 game_count;
	u8 selected_game;
	Arduboy_RuntimeState_t last_state;
	Arduboy_RuntimeResult_t last_result;
	lv_timer_t *poll_timer;
	char status_text[96];
} GUI_ArduboyContext_t;

static int GUI_Arduboy_PageBuild(Page_Struct_t *page);
static int GUI_Arduboy_PageLoaded(Page_Struct_t *page, void *user_data);
static int GUI_Arduboy_EnsureTimer(void);
static void GUI_Arduboy_CreateHeader(GUI_ArduboyContext_t *context);
static void GUI_Arduboy_CreateList(GUI_ArduboyContext_t *context);
static void GUI_Arduboy_BackEvent(lv_event_t *event);
static void GUI_Arduboy_GameEvent(lv_event_t *event);
static void GUI_Arduboy_Poll(lv_timer_t *timer);
static void GUI_Arduboy_SetLocked(GUI_ArduboyContext_t *context, u8 locked);
static void GUI_Arduboy_SetStatus(GUI_ArduboyContext_t *context, const char *text);
static void GUI_Arduboy_RebuildFocus(GUI_ArduboyContext_t *context, lv_obj_t *focus);
static u8 GUI_Arduboy_HandleNavigation(u32 key);

static GUI_ArduboyContext_t s_arduboy;

int GUI_Arduboy_Register(void)
{
	int code;

	if (Page_Find(GUI_PAGE_ARDUBOY) != NULL)
	{
		return GUI_Arduboy_EnsureTimer();
	}

	code = Page_Create(GUI_PAGE_ARDUBOY, "Arduboy", GUI_Arduboy_PageBuild, GUI_Arduboy_PageLoaded);
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	return GUI_Arduboy_EnsureTimer();
}

int GUI_Arduboy_Show(void)
{
	int code;

	if (Page_Find(GUI_PAGE_ARDUBOY) == NULL)
	{
		code = GUI_Arduboy_Register();
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	return Page_Show_Anim(GUI_PAGE_ARDUBOY, LV_SCR_LOAD_ANIM_MOVE_LEFT, 120U, 0U);
}

static int GUI_Arduboy_PageBuild(Page_Struct_t *page)
{
	if (page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}

	memset(&s_arduboy, 0, sizeof(s_arduboy));
	s_arduboy.screen = page->screen;
	s_arduboy.selected_game = 0xFFU;
	s_arduboy.last_state = ARDUBOY_RUNTIME_IDLE;
	s_arduboy.last_result = ARDUBOY_RUNTIME_RESULT_NONE;

	lv_obj_clear_flag(page->screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(page->screen, lv_color_hex(0x101418U), 0);
	lv_obj_set_style_bg_opa(page->screen, LV_OPA_COVER, 0);
	GUI_Arduboy_CreateHeader(&s_arduboy);
	GUI_Arduboy_CreateList(&s_arduboy);
	GUI_Arduboy_SetStatus(&s_arduboy, "Ready");
	return SYSTEM_STATUS_OK;
}

static int GUI_Arduboy_PageLoaded(Page_Struct_t *page, void *user_data)
{
	(void)page;
	(void)user_data;

	GUI_Arduboy_Poll(NULL);
	GUI_Arduboy_RebuildFocus(&s_arduboy,
	                         (s_arduboy.game_count > 0U) ? s_arduboy.game_buttons[0] : s_arduboy.back_button);
	lv_obj_invalidate(s_arduboy.screen);
	return SYSTEM_STATUS_OK;
}

static int GUI_Arduboy_EnsureTimer(void)
{
	if (s_arduboy.poll_timer != NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	s_arduboy.poll_timer = lv_timer_create(GUI_Arduboy_Poll, GUI_ARDUBOY_POLL_MS, NULL);
	return (s_arduboy.poll_timer != NULL) ? SYSTEM_STATUS_OK : OUT_OF_HEAP_MEMORY;
}

static void GUI_Arduboy_CreateHeader(GUI_ArduboyContext_t *context)
{
	lv_obj_t *title;
	lv_obj_t *line;
	lv_obj_t *icon;

	title = lv_label_create(context->screen);
	lv_label_set_text(title, "Arduboy");
	lv_obj_set_style_text_color(title, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 9);

	context->back_button = lv_btn_create(context->screen);
	lv_obj_clear_flag(context->back_button, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(context->back_button, 42, 28);
	lv_obj_set_pos(context->back_button, 268, 6);
	lv_obj_set_style_radius(context->back_button, 8, 0);
	lv_obj_set_style_bg_color(context->back_button, lv_color_hex(0x252D36U), 0);
	lv_obj_set_style_bg_color(context->back_button, lv_color_hex(0x344150U), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(context->back_button, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(context->back_button, 0, 0);
	lv_obj_set_style_border_width(context->back_button, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(context->back_button, lv_color_hex(GUI_ARDUBOY_ACCENT), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_width(context->back_button, 0, 0);
	lv_obj_set_style_shadow_width(context->back_button, 0, 0);
	lv_obj_set_style_pad_all(context->back_button, 0, 0);
	lv_obj_add_event_cb(context->back_button, GUI_Arduboy_BackEvent, LV_EVENT_CLICKED, context);
	lv_obj_add_event_cb(context->back_button, GUI_Arduboy_BackEvent, LV_EVENT_KEY, context);

	icon = lv_label_create(context->back_button);
	lv_label_set_text(icon, LV_SYMBOL_LEFT);
	lv_obj_set_style_text_color(icon, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_center(icon);

	line = lv_obj_create(context->screen);
	lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(line, 296, 1);
	lv_obj_set_pos(line, 12, 36);
	lv_obj_set_style_radius(line, 0, 0);
	lv_obj_set_style_border_width(line, 0, 0);
	lv_obj_set_style_bg_color(line, lv_color_hex(0x303844U), 0);
	lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
}

static void GUI_Arduboy_CreateList(GUI_ArduboyContext_t *context)
{
	const Arduboy_Game_t *game;
	lv_obj_t *button;
	u8 count;
	u8 i;

	context->list = lv_list_create(context->screen);
	lv_obj_set_size(context->list, GUI_ARDUBOY_LIST_W, GUI_ARDUBOY_LIST_H);
	lv_obj_set_pos(context->list, GUI_ARDUBOY_LIST_X, GUI_ARDUBOY_LIST_Y);
	lv_obj_set_style_radius(context->list, 6, 0);
	lv_obj_set_style_border_width(context->list, 1, 0);
	lv_obj_set_style_border_color(context->list, lv_color_hex(0x303844U), 0);
	lv_obj_set_style_bg_color(context->list, lv_color_hex(0x141A20U), 0);
	lv_obj_set_style_bg_opa(context->list, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(context->list, 2, 0);
	lv_obj_set_scrollbar_mode(context->list, LV_SCROLLBAR_MODE_OFF);

	count = Arduboy_Games_Count();
	if (count > GUI_ARDUBOY_MAX_GAMES)
	{
		count = GUI_ARDUBOY_MAX_GAMES;
	}
	context->game_count = count;

	for (i = 0U; i < count; i++)
	{
		game = Arduboy_Games_Get(i);
		if (game == NULL)
		{
			continue;
		}

		context->game_indices[i] = i;
		button = lv_list_add_btn(context->list,
		                         (game->diagnostic != 0U) ? LV_SYMBOL_KEYBOARD : LV_SYMBOL_PLAY,
		                         game->name);
		context->game_buttons[i] = button;
		lv_obj_set_height(button, GUI_ARDUBOY_ROW_H);
		lv_obj_set_style_radius(button, 4, 0);
		lv_obj_set_style_bg_color(button, lv_color_hex(0x182028U), 0);
		lv_obj_set_style_bg_color(button, lv_color_hex(0x263240U), LV_STATE_PRESSED);
		lv_obj_set_style_bg_color(button, lv_color_hex(0x22303AU), LV_STATE_FOCUSED);
		lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
		lv_obj_set_style_border_width(button, 0, 0);
		lv_obj_set_style_border_width(button, 2, LV_STATE_FOCUSED);
		lv_obj_set_style_border_color(button, lv_color_hex(GUI_ARDUBOY_ACCENT), LV_STATE_FOCUSED);
		lv_obj_set_style_outline_width(button, 0, 0);
		lv_obj_set_style_text_color(button, lv_color_hex(0xEEF2F6U), 0);
		lv_obj_set_style_text_color(button, lv_color_hex(0x6F7A86U), LV_STATE_DISABLED);
		lv_obj_add_event_cb(button, GUI_Arduboy_GameEvent, LV_EVENT_CLICKED, &context->game_indices[i]);
		lv_obj_add_event_cb(button, GUI_Arduboy_GameEvent, LV_EVENT_KEY, &context->game_indices[i]);
	}

	context->status_label = lv_label_create(context->screen);
	lv_label_set_long_mode(context->status_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(context->status_label, 296);
	lv_obj_set_pos(context->status_label, 12, GUI_ARDUBOY_STATUS_Y);
	lv_obj_set_style_text_color(context->status_label, lv_color_hex(0x8E98A4U), 0);
}

static void GUI_Arduboy_BackEvent(lv_event_t *event)
{
	lv_event_code_t code = lv_event_get_code(event);

	if (Arduboy_Runtime_IsActive() != 0U)
	{
		return;
	}
	if (code == LV_EVENT_CLICKED ||
	    (code == LV_EVENT_KEY && (lv_event_get_key(event) == LV_KEY_ENTER ||
	                              lv_event_get_key(event) == LV_KEY_ESC)))
	{
		(void)Page_Back();
	}
	else if (code == LV_EVENT_KEY)
	{
		(void)GUI_Arduboy_HandleNavigation(lv_event_get_key(event));
	}
}

static void GUI_Arduboy_GameEvent(lv_event_t *event)
{
	u8 *index = (u8 *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;
	const Arduboy_Game_t *game;

	if (index == NULL)
	{
		return;
	}

	if (code == LV_EVENT_KEY)
	{
		key = lv_event_get_key(event);
		if (key == LV_KEY_ESC || key == LV_KEY_LEFT)
		{
			if (Arduboy_Runtime_IsActive() == 0U) (void)Page_Back();
			return;
		}
		if (key != LV_KEY_ENTER && key != LV_KEY_RIGHT)
		{
			(void)GUI_Arduboy_HandleNavigation(key);
			return;
		}
	}
	else if (code != LV_EVENT_CLICKED)
	{
		return;
	}

	if (Arduboy_Runtime_IsActive() != 0U)
	{
		return;
	}

	game = Arduboy_Games_Get(*index);
	if (game == NULL)
	{
		GUI_Arduboy_SetStatus(&s_arduboy, "Invalid game");
		return;
	}

	s_arduboy.selected_game = *index;
	(void)snprintf(s_arduboy.status_text, sizeof(s_arduboy.status_text), "Starting %s", game->name);
	GUI_Arduboy_SetStatus(&s_arduboy, s_arduboy.status_text);
	GUI_Arduboy_SetLocked(&s_arduboy, 1U);
	if (Arduboy_Runtime_Start(*index) != SYSTEM_STATUS_OK)
	{
		GUI_Arduboy_SetLocked(&s_arduboy, 0U);
		GUI_Arduboy_SetStatus(&s_arduboy, Arduboy_Runtime_ResultText(Arduboy_Runtime_GetResult()));
	}
}

static void GUI_Arduboy_Poll(lv_timer_t *timer)
{
	Arduboy_RuntimeState_t state;
	Arduboy_RuntimeResult_t result;
	Arduboy_PortStats_t stats;
	const Arduboy_Game_t *game;

	(void)timer;
	if (s_arduboy.screen == NULL)
	{
		return;
	}

	state = Arduboy_Runtime_GetState();
	result = Arduboy_Runtime_GetResult();
	if (state == s_arduboy.last_state && result == s_arduboy.last_result)
	{
		return;
	}

	s_arduboy.last_state = state;
	s_arduboy.last_result = result;
	if (state == ARDUBOY_RUNTIME_STARTING)
	{
		game = Arduboy_Games_Get(Arduboy_Runtime_GetGameIndex());
		(void)snprintf(s_arduboy.status_text, sizeof(s_arduboy.status_text),
		               "Starting %s", (game != NULL) ? game->name : "game");
		GUI_Arduboy_SetStatus(&s_arduboy, s_arduboy.status_text);
		GUI_Arduboy_SetLocked(&s_arduboy, 1U);
	}
	else if (state == ARDUBOY_RUNTIME_RUNNING || state == ARDUBOY_RUNTIME_STOPPING)
	{
		GUI_Arduboy_SetStatus(&s_arduboy,
		                      (state == ARDUBOY_RUNTIME_RUNNING) ? "Game running" : "Stopping game");
		GUI_Arduboy_SetLocked(&s_arduboy, 1U);
	}
	else if (state == ARDUBOY_RUNTIME_FINISHED)
	{
		Arduboy_Port_GetStats(&stats);
		(void)snprintf(s_arduboy.status_text, sizeof(s_arduboy.status_text),
		               "%s | C:%lums LCD:%lums CPU:%u%% S:%luB", Arduboy_Runtime_ResultText(result),
		               (unsigned long)stats.max_convert_ms, (unsigned long)stats.max_flush_ms,
		               (unsigned int)stats.max_cpu_load,
		               (unsigned long)Arduboy_Runtime_GetStackSpace());
		GUI_Arduboy_SetStatus(&s_arduboy, s_arduboy.status_text);
		GUI_Arduboy_SetLocked(&s_arduboy, 0U);
	}
	else if (state == ARDUBOY_RUNTIME_ERROR)
	{
		GUI_Arduboy_SetStatus(&s_arduboy, Arduboy_Runtime_ResultText(result));
		GUI_Arduboy_SetLocked(&s_arduboy, 0U);
	}
	else
	{
		GUI_Arduboy_SetStatus(&s_arduboy, "Ready");
		GUI_Arduboy_SetLocked(&s_arduboy, 0U);
	}

	lv_obj_invalidate(s_arduboy.screen);
}

static void GUI_Arduboy_SetLocked(GUI_ArduboyContext_t *context, u8 locked)
{
	u8 i;

	for (i = 0U; i < context->game_count; i++)
	{
		if (context->game_buttons[i] == NULL) continue;
		if (locked != 0U)
		{
			lv_obj_add_state(context->game_buttons[i], LV_STATE_DISABLED);
			lv_obj_clear_flag(context->game_buttons[i], LV_OBJ_FLAG_CLICKABLE);
		}
		else
		{
			lv_obj_clear_state(context->game_buttons[i], LV_STATE_DISABLED);
			lv_obj_add_flag(context->game_buttons[i], LV_OBJ_FLAG_CLICKABLE);
		}
	}

	if (context->back_button != NULL)
	{
		if (locked != 0U)
		{
			lv_obj_add_state(context->back_button, LV_STATE_DISABLED);
			lv_obj_clear_flag(context->back_button, LV_OBJ_FLAG_CLICKABLE);
		}
		else
		{
			lv_obj_clear_state(context->back_button, LV_STATE_DISABLED);
			lv_obj_add_flag(context->back_button, LV_OBJ_FLAG_CLICKABLE);
		}
	}
}

static void GUI_Arduboy_SetStatus(GUI_ArduboyContext_t *context, const char *text)
{
	if (context->status_label != NULL)
	{
		lv_label_set_text(context->status_label, (text != NULL) ? text : "");
	}
}

static void GUI_Arduboy_RebuildFocus(GUI_ArduboyContext_t *context, lv_obj_t *focus)
{
	lv_group_t *group = lv_group_get_default();
	u8 i;

	if (group == NULL)
	{
		return;
	}

	lv_group_remove_all_objs(group);
	lv_group_set_editing(group, false);
	lv_group_set_wrap(group, true);
	for (i = 0U; i < context->game_count; i++)
	{
		if (context->game_buttons[i] != NULL && !lv_obj_has_state(context->game_buttons[i], LV_STATE_DISABLED))
		{
			lv_group_add_obj(group, context->game_buttons[i]);
		}
	}
	if (context->back_button != NULL && !lv_obj_has_state(context->back_button, LV_STATE_DISABLED))
	{
		lv_group_add_obj(group, context->back_button);
	}
	if (focus != NULL && lv_obj_get_group(focus) == group)
	{
		lv_group_focus_obj(focus);
	}
}

static u8 GUI_Arduboy_HandleNavigation(u32 key)
{
	lv_group_t *group = lv_group_get_default();

	if (group == NULL)
	{
		return 0U;
	}
	if (key == LV_KEY_UP || key == LV_KEY_PREV)
	{
		lv_group_focus_prev(group);
		return 1U;
	}
	if (key == LV_KEY_DOWN || key == LV_KEY_NEXT)
	{
		lv_group_focus_next(group);
		return 1U;
	}
	return 0U;
}
