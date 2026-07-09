#include "GUI_Main.h"
#include "GUI_NES.h"
#include "GUI_Player.h"
#include "GUI_Image.h"
#include "GUI_Settings.h"

#define GUI_MENU_ITEM_W 72
#define GUI_MENU_ITEM_H 78
#define GUI_MENU_START_X 8
#define GUI_MENU_START_Y 48
#define GUI_MENU_GAP_X 6
#define GUI_MENU_GAP_Y 8
#define GUI_MENU_COLS 4

typedef struct
{
	Page_ID_t page_id;
	const char *name;
	const char *label;
	const char *symbol;
	u32 color;
} GUI_MenuItem_t;

static int GUI_MainPageBuild(Page_Struct_t *page);
static int GUI_AppShellBuild(Page_Struct_t *page);
static int GUI_PageLoaded(Page_Struct_t *page, void *user_data);
static void GUI_AddFocusableChildren(lv_group_t *group, lv_obj_t *parent, lv_obj_t **first_obj);
static void GUI_MenuFocusSet(u8 index);
static void GUI_MenuFocusMove(s8 row_delta, s8 col_delta);
static u8 GUI_MenuIndexFromObject(lv_obj_t *obj);
static void GUI_MenuButtonEvent(lv_event_t *event);
static void GUI_BackButtonEvent(lv_event_t *event);

static const GUI_MenuItem_t s_menu_items[] =
{
	{GUI_PAGE_NES, "NES", "NES", LV_SYMBOL_PLAY, 0xE84E40U},
	{GUI_PAGE_ARDUBOY, "Arduboy", "Arduboy", LV_SYMBOL_KEYBOARD, 0x43A047U},
	{GUI_PAGE_MUSIC, "Music", "Music", LV_SYMBOL_AUDIO, 0x1E88E5U},
	{GUI_PAGE_VIDEO, "Video Player", "Video", LV_SYMBOL_VIDEO, 0xD81B60U},
	{GUI_PAGE_IMAGE, "Image", "Image", LV_SYMBOL_IMAGE, 0x8E24AAU},
	{GUI_PAGE_TEXT, "Text", "Text", LV_SYMBOL_FILE, 0x546E7AU},
	{GUI_PAGE_AXF_LOADER, "AXF Loader", "AXF", LV_SYMBOL_UPLOAD, 0x00897BU},
	{GUI_PAGE_SETTINGS, "Settings", "Settings", LV_SYMBOL_SETTINGS, 0xF9A825U},
};

#define GUI_MENU_ITEM_COUNT ((u8)(sizeof(s_menu_items) / sizeof(s_menu_items[0])))
#define GUI_MENU_ROWS       ((u8)((GUI_MENU_ITEM_COUNT + GUI_MENU_COLS - 1U) / GUI_MENU_COLS))
#define GUI_MENU_INDEX_NONE 0xFFU

static lv_obj_t *s_menu_buttons[GUI_MENU_ITEM_COUNT];
static u8 s_menu_focus_index = 0U;

static const GUI_MenuItem_t *GUI_FindMenuItem(Page_ID_t id)
{
	u8 i;

	for (i = 0U; i < GUI_MENU_ITEM_COUNT; i++)
	{
		if (s_menu_items[i].page_id == id)
		{
			return &s_menu_items[i];
		}
	}

	return NULL;
}

static void GUI_ApplyScreenBase(lv_obj_t *screen)
{
	lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418U), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

static void GUI_CreateHeader(lv_obj_t *screen, const char *title)
{
	lv_obj_t *label;
	lv_obj_t *line;

	label = lv_label_create(screen);
	lv_label_set_text(label, title);
	lv_obj_set_style_text_color(label, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, 12, 9);

	line = lv_obj_create(screen);
	lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(line, 296, 1);
	lv_obj_set_pos(line, 12, 36);
	lv_obj_set_style_radius(line, 0, 0);
	lv_obj_set_style_border_width(line, 0, 0);
	lv_obj_set_style_bg_color(line, lv_color_hex(0x303844U), 0);
	lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
}

static lv_obj_t *GUI_CreateIconBadge(lv_obj_t *parent, const char *symbol, u32 color, lv_coord_t size)
{
	lv_obj_t *badge;
	lv_obj_t *icon;

	badge = lv_obj_create(parent);
	lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(badge, size, size);
	lv_obj_set_style_radius(badge, 8, 0);
	lv_obj_set_style_border_width(badge, 0, 0);
	lv_obj_set_style_bg_color(badge, lv_color_hex(color), 0);
	lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(badge, 0, 0);

	icon = lv_label_create(badge);
	lv_label_set_text(icon, symbol);
	lv_obj_set_style_text_color(icon, lv_color_white(), 0);
	lv_obj_center(icon);

	return badge;
}

static void GUI_CreateMenuButton(lv_obj_t *screen, const GUI_MenuItem_t *item, u8 index)
{
	u8 col;
	u8 row;
	lv_coord_t x;
	lv_coord_t y;
	lv_obj_t *btn;
	lv_obj_t *badge;
	lv_obj_t *name;

	col = index % GUI_MENU_COLS;
	row = index / GUI_MENU_COLS;
	x = GUI_MENU_START_X + (lv_coord_t)col * (GUI_MENU_ITEM_W + GUI_MENU_GAP_X);
	y = GUI_MENU_START_Y + (lv_coord_t)row * (GUI_MENU_ITEM_H + GUI_MENU_GAP_Y);

	btn = lv_btn_create(screen);
	lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(btn, GUI_MENU_ITEM_W, GUI_MENU_ITEM_H);
	lv_obj_set_pos(btn, x, y);
	lv_obj_set_style_radius(btn, 8, 0);
	lv_obj_set_style_bg_color(btn, lv_color_hex(0x171D24U), 0);
	lv_obj_set_style_bg_color(btn, lv_color_hex(0x222C35U), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(btn, 1, 0);
	lv_obj_set_style_border_width(btn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(btn, lv_color_hex(item->color), 0);
	lv_obj_set_style_outline_width(btn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(btn, 1, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(btn, lv_color_hex(item->color), LV_STATE_FOCUSED);
	lv_obj_set_style_shadow_width(btn, 0, 0);
	lv_obj_set_style_pad_all(btn, 0, 0);
	lv_obj_add_event_cb(btn, GUI_MenuButtonEvent, LV_EVENT_CLICKED, (void *)item);
	lv_obj_add_event_cb(btn, GUI_MenuButtonEvent, LV_EVENT_KEY, (void *)item);
	lv_obj_add_event_cb(btn, GUI_MenuButtonEvent, LV_EVENT_FOCUSED, (void *)item);
	s_menu_buttons[index] = btn;

	badge = GUI_CreateIconBadge(btn, item->symbol, item->color, 34);
	lv_obj_align(badge, LV_ALIGN_TOP_MID, 0, 8);

	name = lv_label_create(btn);
	lv_label_set_text(name, item->label);
	lv_label_set_long_mode(name, LV_LABEL_LONG_CLIP);
	lv_obj_set_width(name, GUI_MENU_ITEM_W - 8);
	lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);
	lv_obj_set_style_text_color(name, lv_color_hex(0xEEF2F6U), 0);
	lv_obj_align(name, LV_ALIGN_BOTTOM_MID, 0, -8);
}

static int GUI_MainPageBuild(Page_Struct_t *page)
{
	u8 i;

	if (page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}

	memset(s_menu_buttons, 0, sizeof(s_menu_buttons));

	GUI_ApplyScreenBase(page->screen);
	GUI_CreateHeader(page->screen, "Star Gamer");

	for (i = 0U; i < GUI_MENU_ITEM_COUNT; i++)
	{
		GUI_CreateMenuButton(page->screen, &s_menu_items[i], i);
	}

	return SYSTEM_STATUS_OK;
}

static int GUI_AppShellBuild(Page_Struct_t *page)
{
	const GUI_MenuItem_t *item;
	const char *name;
	const char *symbol;
	u32 color;
	lv_obj_t *back;
	lv_obj_t *back_icon;
	lv_obj_t *badge;
	lv_obj_t *title;
	lv_obj_t *state;

	if (page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}

	item = GUI_FindMenuItem(page->id);
	name = (page->name != NULL) ? page->name : "App";
	symbol = (item != NULL) ? item->symbol : LV_SYMBOL_DIRECTORY;
	color = (item != NULL) ? item->color : 0x607D8BU;

	GUI_ApplyScreenBase(page->screen);
	GUI_CreateHeader(page->screen, name);

	back = lv_btn_create(page->screen);
	lv_obj_clear_flag(back, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(back, 42, 28);
	lv_obj_set_pos(back, 268, 6);
	lv_obj_set_style_radius(back, 8, 0);
	lv_obj_set_style_bg_color(back, lv_color_hex(0x252D36U), 0);
	lv_obj_set_style_bg_color(back, lv_color_hex(0x344150U), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(back, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(back, 0, 0);
	lv_obj_set_style_border_width(back, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(back, lv_color_hex(color), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_width(back, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(back, 1, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(back, lv_color_hex(color), LV_STATE_FOCUSED);
	lv_obj_set_style_shadow_width(back, 0, 0);
	lv_obj_set_style_pad_all(back, 0, 0);
	lv_obj_add_event_cb(back, GUI_BackButtonEvent, LV_EVENT_CLICKED, NULL);

	back_icon = lv_label_create(back);
	lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
	lv_obj_set_style_text_color(back_icon, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_center(back_icon);

	badge = GUI_CreateIconBadge(page->screen, symbol, color, 72);
	lv_obj_align(badge, LV_ALIGN_CENTER, 0, -18);

	title = lv_label_create(page->screen);
	lv_label_set_text(title, name);
	lv_obj_set_style_text_color(title, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_align(title, LV_ALIGN_CENTER, 0, 36);

	state = lv_label_create(page->screen);
	lv_label_set_text(state, "Ready");
	lv_obj_set_style_text_color(state, lv_color_hex(0x8E98A4U), 0);
	lv_obj_align(state, LV_ALIGN_CENTER, 0, 58);

	return SYSTEM_STATUS_OK;
}

static int GUI_PageLoaded(Page_Struct_t *page, void *user_data)
{
	lv_group_t *group;
	lv_obj_t *first_obj = NULL;

	(void)user_data;

	if (page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}

	group = lv_group_get_default();
	if (group == NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	lv_group_remove_all_objs(group);
	lv_group_set_editing(group, false);
	lv_group_set_wrap(group, true);

	GUI_AddFocusableChildren(group, page->screen, &first_obj);
	if (page->id == GUI_PAGE_MAIN_MENU && s_menu_buttons[s_menu_focus_index] != NULL)
	{
		GUI_MenuFocusSet(s_menu_focus_index);
	}
	else if (first_obj != NULL)
	{
		lv_group_focus_obj(first_obj);
	}

	lv_obj_invalidate(page->screen);

	return SYSTEM_STATUS_OK;
}

static void GUI_AddFocusableChildren(lv_group_t *group, lv_obj_t *parent, lv_obj_t **first_obj)
{
	u32 i;
	u32 child_count;

	if (group == NULL || parent == NULL)
	{
		return;
	}

	child_count = lv_obj_get_child_cnt(parent);
	for (i = 0U; i < child_count; i++)
	{
		lv_obj_t *child = lv_obj_get_child(parent, (s32)i);

		if (lv_obj_is_group_def(child) && !lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN))
		{
			lv_group_add_obj(group, child);
			if (first_obj != NULL && *first_obj == NULL)
			{
				*first_obj = child;
			}
		}

		GUI_AddFocusableChildren(group, child, first_obj);
	}
}

static void GUI_MenuFocusSet(u8 index)
{
	if (index >= GUI_MENU_ITEM_COUNT || s_menu_buttons[index] == NULL)
	{
		return;
	}

	s_menu_focus_index = index;
	lv_group_focus_obj(s_menu_buttons[index]);
	lv_obj_scroll_to_view(s_menu_buttons[index], LV_ANIM_OFF);
	lv_obj_invalidate(lv_scr_act());
}

static void GUI_MenuFocusMove(s8 row_delta, s8 col_delta)
{
	s16 row;
	s16 col;
	s16 target;

	row = (s16)(s_menu_focus_index / GUI_MENU_COLS);
	col = (s16)(s_menu_focus_index % GUI_MENU_COLS);

	if (row_delta != 0)
	{
		row += row_delta;
		if (row < 0)
		{
			row = (s16)GUI_MENU_ROWS - 1;
		}
		else if (row >= (s16)GUI_MENU_ROWS)
		{
			row = 0;
		}
	}

	if (col_delta != 0)
	{
		col += col_delta;
		if (col < 0)
		{
			col = (s16)GUI_MENU_COLS - 1;
		}
		else if (col >= (s16)GUI_MENU_COLS)
		{
			col = 0;
		}
	}

	target = row * GUI_MENU_COLS + col;
	while (target >= (s16)GUI_MENU_ITEM_COUNT)
	{
		target -= GUI_MENU_COLS;
		if (target < 0)
		{
			target = 0;
			break;
		}
	}

	GUI_MenuFocusSet((u8)target);
}

static u8 GUI_MenuIndexFromObject(lv_obj_t *obj)
{
	u8 i;

	for (i = 0U; i < GUI_MENU_ITEM_COUNT; i++)
	{
		if (s_menu_buttons[i] == obj)
		{
			return i;
		}
	}

	return GUI_MENU_INDEX_NONE;
}

static int GUI_CreatePageIfMissing(Page_ID_t id, const char *name, PageInit init)
{
	if (Page_Find(id) != NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	return Page_Create(id, name, init, GUI_PageLoaded);
}

int GUI_Main_Init(void)
{
	int code;
	u8 i;

	code = Page_Manager_Init();
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	code = GUI_CreatePageIfMissing(GUI_PAGE_MAIN_MENU, "Main Menu", GUI_MainPageBuild);
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	code = GUI_NES_Register();
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	code = GUI_Player_Register();
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	code = GUI_Image_Register();
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	code = GUI_Settings_Register();
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	for (i = 0U; i < GUI_MENU_ITEM_COUNT; i++)
	{
		if (s_menu_items[i].page_id == GUI_PAGE_NES ||
		    s_menu_items[i].page_id == GUI_PAGE_MUSIC ||
		    s_menu_items[i].page_id == GUI_PAGE_IMAGE ||
		    s_menu_items[i].page_id == GUI_PAGE_SETTINGS)
		{
			continue;
		}

		code = GUI_CreatePageIfMissing(s_menu_items[i].page_id, s_menu_items[i].name, GUI_AppShellBuild);
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	return GUI_Main_Show();
}

int GUI_Main_Show(void)
{
	return Page_Show(GUI_PAGE_MAIN_MENU);
}

static void GUI_MenuButtonEvent(lv_event_t *event)
{
	const GUI_MenuItem_t *item;
	lv_event_code_t code;
	u32 key;
	u8 index;

	code = lv_event_get_code(event);
	if (code == LV_EVENT_FOCUSED)
	{
		index = GUI_MenuIndexFromObject(lv_event_get_target(event));
		if (index != GUI_MENU_INDEX_NONE)
		{
			s_menu_focus_index = index;
		}
		return;
	}

	if (code == LV_EVENT_KEY)
	{
		key = lv_event_get_key(event);
		if (key == LV_KEY_UP)
		{
			GUI_MenuFocusMove(-1, 0);
		}
		else if (key == LV_KEY_DOWN)
		{
			GUI_MenuFocusMove(1, 0);
		}
		else if (key == LV_KEY_LEFT)
		{
			GUI_MenuFocusMove(0, -1);
		}
		else if (key == LV_KEY_RIGHT)
		{
			GUI_MenuFocusMove(0, 1);
		}
		return;
	}

	if (code != LV_EVENT_CLICKED)
	{
		return;
	}

	item = (const GUI_MenuItem_t *)lv_event_get_user_data(event);
	if (item != NULL)
	{
		(void)Page_Show_Anim(item->page_id, LV_SCR_LOAD_ANIM_MOVE_LEFT, 120U, 0U);
	}
}

static void GUI_BackButtonEvent(lv_event_t *event)
{
	if (lv_event_get_code(event) == LV_EVENT_CLICKED)
	{
		(void)Page_Back();
	}
}
