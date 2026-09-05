#include "GUI_Arduboy.h"

#include "Arduboy_Games.h"
#include "Arduboy_Runtime.h"
#include "GUI_Theme.h"
#include "System_Status.h"

#include <stdio.h>
#include <string.h>

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
	u8 locked;
	char status_text[96];
} GUI_ArduboyContext_t;

static int GUI_Arduboy_PageBuild(Page_Struct_t *page);
static int GUI_Arduboy_PageLoaded(Page_Struct_t *page, void *user_data);
static int GUI_Arduboy_EnsureTimer(void);
static void GUI_Arduboy_CreateHeader(GUI_ArduboyContext_t *context);
static void GUI_Arduboy_CreateList(GUI_ArduboyContext_t *context);
static void GUI_Arduboy_ApplyIconFont(lv_obj_t *row);
static void GUI_Arduboy_BackEvent(lv_event_t *event);
static void GUI_Arduboy_GameEvent(lv_event_t *event);
static void GUI_Arduboy_Poll(lv_timer_t *timer);
static void GUI_Arduboy_SetLocked(GUI_ArduboyContext_t *context, u8 locked);
static void GUI_Arduboy_SetStatus(GUI_ArduboyContext_t *context, const char *text);
static void GUI_Arduboy_RebuildFocus(GUI_ArduboyContext_t *context, lv_obj_t *focus);
static void GUI_Arduboy_RestoreFocus(GUI_ArduboyContext_t *context);
static void GUI_Arduboy_WaitInputRelease(void);
static int GUI_Arduboy_ReturnToMain(void);
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

	return Page_Show_Anim(GUI_PAGE_ARDUBOY, LV_SCR_LOAD_ANIM_MOVE_LEFT, GUI_M3_MOTION_PAGE, 0U);
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

	GUI_M3_ApplyScreen(page->screen);
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
	if (Arduboy_Runtime_IsActive() == 0U && s_arduboy.locked == 0U)
	{
		GUI_Arduboy_RestoreFocus(&s_arduboy);
	}
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
	lv_obj_t *bar;

	bar = GUI_M3_CreateTopAppBar(context->screen, "Arduboy");
	context->back_button = GUI_M3_AppBarSetNav(bar, GUI_ICON_ARROW_BACK, GUI_Arduboy_BackEvent, context);
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
	GUI_M3_StyleList(context->list);
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
		                         (game->diagnostic != 0U) ? GUI_ICON_BUG : GUI_ICON_PLAY,
		                         game->name);
		context->game_buttons[i] = button;
		lv_obj_set_height(button, GUI_ARDUBOY_ROW_H);
		GUI_M3_StyleListItem(button);
		GUI_Arduboy_ApplyIconFont(button);
		lv_obj_add_event_cb(button, GUI_Arduboy_GameEvent, LV_EVENT_CLICKED, &context->game_indices[i]);
		lv_obj_add_event_cb(button, GUI_Arduboy_GameEvent, LV_EVENT_KEY, &context->game_indices[i]);
	}

	context->status_label = lv_label_create(context->screen);
	lv_label_set_long_mode(context->status_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(context->status_label, 292);
	lv_obj_set_pos(context->status_label, 14, GUI_ARDUBOY_STATUS_Y);
	lv_obj_set_style_text_font(context->status_label, GUI_M3_Font(GUI_M3_TYPE_LABEL), 0);
	lv_obj_set_style_text_color(context->status_label, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE_VARIANT), 0);
}

/* The leading glyph of a list row is drawn from an lv_img symbol source, so it
 * needs the Material Symbols font instead of the inherited text font. */
static void GUI_Arduboy_ApplyIconFont(lv_obj_t *row)
{
	lv_obj_t *icon_obj;

	if (row == NULL || lv_obj_get_child_cnt(row) == 0U)
	{
		return;
	}

	icon_obj = lv_obj_get_child(row, 0);
	if (icon_obj != NULL)
	{
		lv_obj_set_style_text_font(icon_obj, GUI_M3_IconFont(), 0);
		lv_obj_set_style_text_color(icon_obj, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE_VARIANT), 0);
		lv_obj_set_style_text_color(icon_obj, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SECONDARY_CONTAINER),
		                            LV_STATE_FOCUSED);
	}
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
		GUI_Arduboy_WaitInputRelease();
		(void)GUI_Arduboy_ReturnToMain();
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
			if (Arduboy_Runtime_IsActive() == 0U)
			{
				GUI_Arduboy_WaitInputRelease();
				(void)GUI_Arduboy_ReturnToMain();
			}
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
	/* Force the next poll to consume completion even for a very short run. */
	s_arduboy.last_state = ARDUBOY_RUNTIME_IDLE;
	s_arduboy.last_result = ARDUBOY_RUNTIME_RESULT_NONE;
	if (Arduboy_Runtime_Start(*index) != SYSTEM_STATUS_OK)
	{
		GUI_Arduboy_SetLocked(&s_arduboy, 0U);
		GUI_Arduboy_SetStatus(&s_arduboy, Arduboy_Runtime_ResultText(Arduboy_Runtime_GetResult()));
		GUI_Arduboy_RestoreFocus(&s_arduboy);
	}
}

static void GUI_Arduboy_Poll(lv_timer_t *timer)
{
	Arduboy_RuntimeState_t state;
	Arduboy_RuntimeResult_t result;
	Arduboy_PortStats_t stats;
	const Arduboy_Game_t *game;
	u8 active;
	u8 desired_locked;

	(void)timer;
	Arduboy_Runtime_Cleanup();
	if (s_arduboy.screen == NULL)
	{
		return;
	}

	state = Arduboy_Runtime_GetState();
	result = Arduboy_Runtime_GetResult();
	active = Arduboy_Runtime_IsActive();
	desired_locked = (active != 0U) ? 1U : 0U;
	if (state == s_arduboy.last_state &&
	    result == s_arduboy.last_result &&
	    s_arduboy.locked == desired_locked)
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
		GUI_Arduboy_RestoreFocus(&s_arduboy);
	}
	else if (state == ARDUBOY_RUNTIME_ERROR)
	{
		GUI_Arduboy_SetStatus(&s_arduboy, Arduboy_Runtime_ResultText(result));
		GUI_Arduboy_SetLocked(&s_arduboy, 0U);
		GUI_Arduboy_RestoreFocus(&s_arduboy);
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

	if (context == NULL)
	{
		return;
	}

	context->locked = (locked != 0U) ? 1U : 0U;
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

static void GUI_Arduboy_RestoreFocus(GUI_ArduboyContext_t *context)
{
	lv_obj_t *focus;

	if (context == NULL || Page_GetCurrent() != GUI_PAGE_ARDUBOY)
	{
		return;
	}

	focus = (context->game_count > 0U) ? context->game_buttons[0] : context->back_button;
	if (context->selected_game < context->game_count &&
	    context->game_buttons[context->selected_game] != NULL)
	{
		focus = context->game_buttons[context->selected_game];
	}

	GUI_Arduboy_RebuildFocus(context, focus);
}

static void GUI_Arduboy_WaitInputRelease(void)
{
	lv_indev_t *indev = lv_indev_get_act();

	if (indev != NULL)
	{
		lv_indev_wait_release(indev);
	}
}

static int GUI_Arduboy_ReturnToMain(void)
{
	Page_ClearHistory();
	return Page_Back();
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
