#include "GUI_Settings.h"
#include "GUI_Font.h"
#include "System_Status.h"
#include "w25q64.h"

#include <stdio.h>
#include <string.h>

#define GUI_SETTINGS_ACCENT          0xF9A825U
#define GUI_SETTINGS_SCREEN_W        320
#define GUI_SETTINGS_HEADER_LINE_W   296
#define GUI_SETTINGS_BACK_X          268
#define GUI_SETTINGS_LIST_X          10
#define GUI_SETTINGS_LIST_Y          54
#define GUI_SETTINGS_LIST_W          300
#define GUI_SETTINGS_LIST_H          150
#define GUI_SETTINGS_ROW_H           34
#define GUI_SETTINGS_STATUS_Y        214
#define GUI_SETTINGS_ABOUT_X         10
#define GUI_SETTINGS_ABOUT_Y         48
#define GUI_SETTINGS_ABOUT_W         300
#define GUI_SETTINGS_ABOUT_H         182
#define GUI_SETTINGS_TEXT_LEN        256U

typedef enum
{
	GUI_SETTINGS_ACTION_FONT_INSTALL = 0,
	GUI_SETTINGS_ACTION_FONT_UNINSTALL,
	GUI_SETTINGS_ACTION_UPDATE,
	GUI_SETTINGS_ACTION_ABOUT
} GUI_SettingsAction_t;

typedef struct
{
	GUI_SettingsAction_t action;
	const char *symbol;
	const char *text;
} GUI_SettingsItem_t;

typedef struct
{
	Page_Struct_t *page;
	lv_obj_t *screen;
	lv_obj_t *back_btn;
	lv_obj_t *list;
	lv_obj_t *status_label;
	lv_obj_t *about_panel;
	lv_obj_t *about_label;
	lv_obj_t *about_close_btn;
	lv_obj_t *font_install_btn;
	lv_obj_t *font_uninstall_btn;
	lv_obj_t *about_btn;
	lv_obj_t *first_item_btn;
	char about_text[GUI_SETTINGS_TEXT_LEN];
	u8 about_visible;
} GUI_SettingsContext_t;

static int GUI_Settings_PageBuild(Page_Struct_t *page);
static int GUI_Settings_PageLoaded(Page_Struct_t *page, void *user_data);
static void GUI_Settings_ApplyScreenBase(lv_obj_t *screen);
static void GUI_Settings_CreateHeader(GUI_SettingsContext_t *context);
static void GUI_Settings_CreateToolButton(lv_obj_t **btn, lv_obj_t *parent, const char *symbol, lv_coord_t x, lv_event_cb_t cb);
static void GUI_Settings_CreateList(GUI_SettingsContext_t *context);
static void GUI_Settings_AddListItem(GUI_SettingsContext_t *context, const GUI_SettingsItem_t *item);
static void GUI_Settings_CreateAboutPanel(GUI_SettingsContext_t *context);
static void GUI_Settings_BackEvent(lv_event_t *event);
static void GUI_Settings_ItemEvent(lv_event_t *event);
static void GUI_Settings_AboutCloseEvent(lv_event_t *event);
static void GUI_Settings_WaitInputRelease(void);
static u8 GUI_Settings_HandleKey(u32 key);
static void GUI_Settings_RunAction(GUI_SettingsContext_t *context, GUI_SettingsAction_t action);
static void GUI_Settings_ShowAbout(GUI_SettingsContext_t *context);
static void GUI_Settings_HideAbout(GUI_SettingsContext_t *context);
static void GUI_Settings_UpdateAboutText(GUI_SettingsContext_t *context);
static void GUI_Settings_UpdateFontState(GUI_SettingsContext_t *context);
static void GUI_Settings_SetListButtonEnabled(lv_obj_t *btn, u8 enabled);
static void GUI_Settings_SetStatus(GUI_SettingsContext_t *context, const char *text);
static void GUI_Settings_RebuildFocus(GUI_SettingsContext_t *context, lv_obj_t *focus_obj);
static void GUI_Settings_AddFocusableChildren(lv_group_t *group, lv_obj_t *parent, lv_obj_t **first_obj);

static GUI_SettingsContext_t s_settings;

static const GUI_SettingsItem_t s_setting_items[] =
{
	{GUI_SETTINGS_ACTION_FONT_INSTALL, LV_SYMBOL_FILE, "Install Font"},
	{GUI_SETTINGS_ACTION_FONT_UNINSTALL, LV_SYMBOL_TRASH, "Uninstall Font"},
	{GUI_SETTINGS_ACTION_UPDATE, LV_SYMBOL_DOWNLOAD, "Install Update"},
	{GUI_SETTINGS_ACTION_ABOUT, LV_SYMBOL_SETTINGS, "About"}
};

int GUI_Settings_Register(void)
{
	if (Page_Find(GUI_PAGE_SETTINGS) != NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	return Page_Create(GUI_PAGE_SETTINGS, "Settings", GUI_Settings_PageBuild, GUI_Settings_PageLoaded);
}

int GUI_Settings_Show(void)
{
	int code;

	if (Page_Find(GUI_PAGE_SETTINGS) == NULL)
	{
		code = GUI_Settings_Register();
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	return Page_Show_Anim(GUI_PAGE_SETTINGS, LV_SCR_LOAD_ANIM_MOVE_LEFT, 120U, 0U);
}

static int GUI_Settings_PageBuild(Page_Struct_t *page)
{
	if (page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}

	memset(&s_settings, 0, sizeof(s_settings));
	s_settings.page = page;
	s_settings.screen = page->screen;

	GUI_Settings_ApplyScreenBase(page->screen);
	GUI_Settings_CreateHeader(&s_settings);
	GUI_Settings_CreateList(&s_settings);
	GUI_Settings_CreateAboutPanel(&s_settings);
	GUI_Settings_SetStatus(&s_settings, "Ready");
	GUI_Settings_UpdateFontState(&s_settings);

	return SYSTEM_STATUS_OK;
}

static int GUI_Settings_PageLoaded(Page_Struct_t *page, void *user_data)
{
	(void)page;
	(void)user_data;

	GUI_Settings_UpdateFontState(&s_settings);
	GUI_Settings_RebuildFocus(&s_settings, s_settings.first_item_btn);
	GUI_Settings_WaitInputRelease();
	lv_obj_invalidate(s_settings.screen);

	return SYSTEM_STATUS_OK;
}

static void GUI_Settings_ApplyScreenBase(lv_obj_t *screen)
{
	lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(screen, lv_color_hex(0x101418U), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
}

static void GUI_Settings_CreateHeader(GUI_SettingsContext_t *context)
{
	lv_obj_t *label;
	lv_obj_t *line;

	label = lv_label_create(context->screen);
	lv_label_set_text(label, "Settings");
	lv_obj_set_style_text_color(label, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_align(label, LV_ALIGN_TOP_LEFT, 12, 9);

	GUI_Settings_CreateToolButton(&context->back_btn, context->screen, LV_SYMBOL_LEFT, GUI_SETTINGS_BACK_X, GUI_Settings_BackEvent);

	line = lv_obj_create(context->screen);
	lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(line, GUI_SETTINGS_HEADER_LINE_W, 1);
	lv_obj_set_pos(line, 12, 36);
	lv_obj_set_style_radius(line, 0, 0);
	lv_obj_set_style_border_width(line, 0, 0);
	lv_obj_set_style_bg_color(line, lv_color_hex(0x303844U), 0);
	lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);
}

static void GUI_Settings_CreateToolButton(lv_obj_t **btn, lv_obj_t *parent, const char *symbol, lv_coord_t x, lv_event_cb_t cb)
{
	lv_obj_t *icon;

	*btn = lv_btn_create(parent);
	lv_obj_clear_flag(*btn, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(*btn, 42, 28);
	lv_obj_set_pos(*btn, x, 6);
	lv_obj_set_style_radius(*btn, 8, 0);
	lv_obj_set_style_bg_color(*btn, lv_color_hex(0x252D36U), 0);
	lv_obj_set_style_bg_color(*btn, lv_color_hex(0x344150U), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(*btn, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(*btn, 0, 0);
	lv_obj_set_style_border_width(*btn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(*btn, lv_color_hex(GUI_SETTINGS_ACCENT), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_width(*btn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(*btn, 1, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(*btn, lv_color_hex(GUI_SETTINGS_ACCENT), LV_STATE_FOCUSED);
	lv_obj_set_style_shadow_width(*btn, 0, 0);
	lv_obj_set_style_pad_all(*btn, 0, 0);
	lv_obj_add_event_cb(*btn, cb, LV_EVENT_CLICKED, &s_settings);
	lv_obj_add_event_cb(*btn, cb, LV_EVENT_KEY, &s_settings);

	icon = lv_label_create(*btn);
	lv_label_set_text(icon, symbol);
	lv_obj_set_style_text_color(icon, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_center(icon);
}

static void GUI_Settings_CreateList(GUI_SettingsContext_t *context)
{
	u8 i;

	context->list = lv_list_create(context->screen);
	lv_obj_set_size(context->list, GUI_SETTINGS_LIST_W, GUI_SETTINGS_LIST_H);
	lv_obj_set_pos(context->list, GUI_SETTINGS_LIST_X, GUI_SETTINGS_LIST_Y);
	lv_obj_set_style_radius(context->list, 6, 0);
	lv_obj_set_style_border_width(context->list, 1, 0);
	lv_obj_set_style_border_color(context->list, lv_color_hex(0x303844U), 0);
	lv_obj_set_style_bg_color(context->list, lv_color_hex(0x141A20U), 0);
	lv_obj_set_style_bg_opa(context->list, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(context->list, 2, 0);
	lv_obj_set_scrollbar_mode(context->list, LV_SCROLLBAR_MODE_OFF);

	for (i = 0U; i < (u8)(sizeof(s_setting_items) / sizeof(s_setting_items[0])); i++)
	{
		GUI_Settings_AddListItem(context, &s_setting_items[i]);
	}

	context->status_label = lv_label_create(context->screen);
	lv_label_set_long_mode(context->status_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(context->status_label, GUI_SETTINGS_SCREEN_W - 24);
	lv_obj_set_pos(context->status_label, 12, GUI_SETTINGS_STATUS_Y);
	lv_obj_set_style_text_color(context->status_label, lv_color_hex(0x8E98A4U), 0);
}

static void GUI_Settings_AddListItem(GUI_SettingsContext_t *context, const GUI_SettingsItem_t *item)
{
	lv_obj_t *btn;
	u8 disabled = 0U;

	if ((item->action == GUI_SETTINGS_ACTION_FONT_INSTALL && GUI_Font_IsInstalled() != 0U) ||
	    (item->action == GUI_SETTINGS_ACTION_FONT_UNINSTALL && GUI_Font_IsInstalled() == 0U))
	{
		disabled = 1U;
	}

	btn = lv_list_add_btn(context->list, item->symbol, item->text);
	lv_obj_set_height(btn, GUI_SETTINGS_ROW_H);
	lv_obj_set_style_radius(btn, 4, 0);
	lv_obj_set_style_bg_color(btn, lv_color_hex(0x182028U), 0);
	lv_obj_set_style_bg_color(btn, lv_color_hex(0x263240U), LV_STATE_PRESSED);
	lv_obj_set_style_bg_color(btn, lv_color_hex(0x22303AU), LV_STATE_FOCUSED);
	lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(btn, 0, 0);
	lv_obj_set_style_border_width(btn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(btn, lv_color_hex(GUI_SETTINGS_ACCENT), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_width(btn, 0, 0);
	lv_obj_set_style_text_color(btn, lv_color_hex(0xEEF2F6U), 0);
	lv_obj_set_style_text_color(btn, lv_color_white(), LV_STATE_FOCUSED);
	lv_obj_set_style_text_color(btn, lv_color_hex(0x6F7A86U), LV_STATE_DISABLED);
	lv_obj_set_style_bg_color(btn, lv_color_hex(0x151A20U), LV_STATE_DISABLED);
	lv_obj_add_event_cb(btn, GUI_Settings_ItemEvent, LV_EVENT_CLICKED, (void *)item);
	lv_obj_add_event_cb(btn, GUI_Settings_ItemEvent, LV_EVENT_KEY, (void *)item);

	if (disabled != 0U)
	{
		lv_obj_add_state(btn, LV_STATE_DISABLED);
		lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
	}

	if (context->first_item_btn == NULL && disabled == 0U)
	{
		context->first_item_btn = btn;
	}

	if (item->action == GUI_SETTINGS_ACTION_FONT_INSTALL)
	{
		context->font_install_btn = btn;
	}
	else if (item->action == GUI_SETTINGS_ACTION_FONT_UNINSTALL)
	{
		context->font_uninstall_btn = btn;
	}

	if (item->action == GUI_SETTINGS_ACTION_ABOUT)
	{
		context->about_btn = btn;
	}
}

static void GUI_Settings_CreateAboutPanel(GUI_SettingsContext_t *context)
{
	lv_obj_t *title;
	lv_obj_t *close_icon;

	context->about_panel = lv_obj_create(context->screen);
	lv_obj_clear_flag(context->about_panel, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(context->about_panel, GUI_SETTINGS_ABOUT_W, GUI_SETTINGS_ABOUT_H);
	lv_obj_set_pos(context->about_panel, GUI_SETTINGS_ABOUT_X, GUI_SETTINGS_ABOUT_Y);
	lv_obj_set_style_radius(context->about_panel, 6, 0);
	lv_obj_set_style_border_width(context->about_panel, 1, 0);
	lv_obj_set_style_border_color(context->about_panel, lv_color_hex(GUI_SETTINGS_ACCENT), 0);
	lv_obj_set_style_bg_color(context->about_panel, lv_color_hex(0x141A20U), 0);
	lv_obj_set_style_bg_opa(context->about_panel, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(context->about_panel, 0, 0);

	title = lv_label_create(context->about_panel);
	lv_label_set_text(title, "About");
	lv_obj_set_style_text_color(title, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_set_pos(title, 10, 8);

	context->about_close_btn = lv_btn_create(context->about_panel);
	lv_obj_clear_flag(context->about_close_btn, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(context->about_close_btn, 32, 24);
	lv_obj_set_pos(context->about_close_btn, 258, 5);
	lv_obj_set_style_radius(context->about_close_btn, 6, 0);
	lv_obj_set_style_bg_color(context->about_close_btn, lv_color_hex(0x252D36U), 0);
	lv_obj_set_style_bg_color(context->about_close_btn, lv_color_hex(0x344150U), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(context->about_close_btn, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(context->about_close_btn, 0, 0);
	lv_obj_set_style_border_width(context->about_close_btn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(context->about_close_btn, lv_color_hex(GUI_SETTINGS_ACCENT), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_width(context->about_close_btn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(context->about_close_btn, 1, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(context->about_close_btn, lv_color_hex(GUI_SETTINGS_ACCENT), LV_STATE_FOCUSED);
	lv_obj_set_style_shadow_width(context->about_close_btn, 0, 0);
	lv_obj_set_style_pad_all(context->about_close_btn, 0, 0);
	lv_obj_add_event_cb(context->about_close_btn, GUI_Settings_AboutCloseEvent, LV_EVENT_CLICKED, context);
	lv_obj_add_event_cb(context->about_close_btn, GUI_Settings_AboutCloseEvent, LV_EVENT_KEY, context);

	close_icon = lv_label_create(context->about_close_btn);
	lv_label_set_text(close_icon, LV_SYMBOL_CLOSE);
	lv_obj_set_style_text_color(close_icon, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_center(close_icon);

	context->about_label = lv_label_create(context->about_panel);
	lv_label_set_long_mode(context->about_label, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(context->about_label, 280);
	lv_obj_set_pos(context->about_label, 10, 32);
	lv_obj_set_style_text_color(context->about_label, lv_color_hex(0xDCE3EAU), 0);
	lv_obj_set_style_text_line_space(context->about_label, 1, 0);

	GUI_Settings_HideAbout(context);
}

static void GUI_Settings_BackEvent(lv_event_t *event)
{
	GUI_SettingsContext_t *context = (GUI_SettingsContext_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;

	if (context == NULL)
	{
		return;
	}

	if (code == LV_EVENT_CLICKED)
	{
		if (context->about_visible != 0U)
		{
			GUI_Settings_HideAbout(context);
		}
		else
		{
			(void)Page_Back();
		}
	}
	else if (code == LV_EVENT_KEY)
	{
		key = lv_event_get_key(event);
		if (key == LV_KEY_ESC)
		{
			GUI_Settings_WaitInputRelease();
			(void)Page_Back();
		}
		else if (key != LV_KEY_ENTER)
		{
			(void)GUI_Settings_HandleKey(key);
		}
	}
}

static void GUI_Settings_ItemEvent(lv_event_t *event)
{
	const GUI_SettingsItem_t *item = (const GUI_SettingsItem_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;

	if (item == NULL)
	{
		return;
	}

	if (s_settings.about_visible != 0U)
	{
		if (code == LV_EVENT_KEY)
		{
			key = lv_event_get_key(event);
			if (key == LV_KEY_ESC || key == LV_KEY_LEFT)
			{
				GUI_Settings_HideAbout(&s_settings);
			}
		}
		return;
	}

	if (code == LV_EVENT_CLICKED || (code == LV_EVENT_KEY && lv_event_get_key(event) == LV_KEY_ENTER) ||
	    (code == LV_EVENT_KEY && lv_event_get_key(event) == LV_KEY_RIGHT))
	{
		GUI_Settings_RunAction(&s_settings, item->action);
	}
	else if (code == LV_EVENT_KEY && (lv_event_get_key(event) == LV_KEY_ESC || lv_event_get_key(event) == LV_KEY_LEFT))
	{
		(void)Page_Back();
	}
	else if (code == LV_EVENT_KEY)
	{
		(void)GUI_Settings_HandleKey(lv_event_get_key(event));
	}
}

static void GUI_Settings_AboutCloseEvent(lv_event_t *event)
{
	GUI_SettingsContext_t *context = (GUI_SettingsContext_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);

	if (context == NULL)
	{
		return;
	}

	if (code == LV_EVENT_CLICKED || (code == LV_EVENT_KEY && (lv_event_get_key(event) == LV_KEY_ENTER ||
	    lv_event_get_key(event) == LV_KEY_ESC || lv_event_get_key(event) == LV_KEY_LEFT)))
	{
		GUI_Settings_HideAbout(context);
	}
	else if (code == LV_EVENT_KEY)
	{
		(void)GUI_Settings_HandleKey(lv_event_get_key(event));
	}
}

static void GUI_Settings_WaitInputRelease(void)
{
	lv_indev_t *indev;

	indev = lv_indev_get_act();
	if (indev != NULL)
	{
		lv_indev_wait_release(indev);
	}
}

static u8 GUI_Settings_HandleKey(u32 key)
{
	lv_group_t *group;

	group = lv_group_get_default();
	if (group == NULL)
	{
		return 0U;
	}

	if (key == LV_KEY_UP || key == LV_KEY_LEFT || key == LV_KEY_PREV)
	{
		lv_group_focus_prev(group);
		return 1U;
	}

	if (key == LV_KEY_DOWN || key == LV_KEY_RIGHT || key == LV_KEY_NEXT)
	{
		lv_group_focus_next(group);
		return 1U;
	}

	return 0U;
}

static void GUI_Settings_RunAction(GUI_SettingsContext_t *context, GUI_SettingsAction_t action)
{
	if (context == NULL)
	{
		return;
	}

	switch (action)
	{
		case GUI_SETTINGS_ACTION_FONT_INSTALL:
			if (GUI_Font_IsInstalled() != 0U)
			{
				GUI_Settings_UpdateFontState(context);
				GUI_Settings_SetStatus(context, "Font already installed");
			}
			else
			{
				GUI_Settings_WaitInputRelease();
				if (GUI_Font_Show() == SYSTEM_STATUS_OK)
				{
					GUI_Settings_SetStatus(context, "Select XBF font bin");
				}
				else
				{
					GUI_Settings_SetStatus(context, "Font installer failed");
				}
			}
			break;
		case GUI_SETTINGS_ACTION_FONT_UNINSTALL:
			GUI_Settings_WaitInputRelease();
			if (GUI_Font_Uninstall() == SYSTEM_STATUS_OK)
			{
				GUI_Settings_UpdateFontState(context);
				GUI_Settings_SetStatus(context, "Font uninstalled");
				GUI_Settings_RebuildFocus(context, context->font_install_btn);
			}
			else
			{
				GUI_Settings_UpdateFontState(context);
				GUI_Settings_SetStatus(context, "Font uninstall failed");
				GUI_Settings_RebuildFocus(context, context->font_uninstall_btn);
			}
			break;
		case GUI_SETTINGS_ACTION_UPDATE:
			GUI_Settings_SetStatus(context, "Update installer is not ready");
			break;
		case GUI_SETTINGS_ACTION_ABOUT:
			GUI_Settings_ShowAbout(context);
			break;
		default:
			break;
	}
}

static void GUI_Settings_ShowAbout(GUI_SettingsContext_t *context)
{
	if (context == NULL || context->about_panel == NULL)
	{
		return;
	}

	GUI_Settings_UpdateAboutText(context);
	lv_obj_clear_flag(context->about_panel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(context->about_panel);
	context->about_visible = 1U;
	GUI_Settings_SetStatus(context, "About");
	GUI_Settings_RebuildFocus(context, context->about_close_btn);
	GUI_Settings_WaitInputRelease();
}

static void GUI_Settings_HideAbout(GUI_SettingsContext_t *context)
{
	if (context == NULL || context->about_panel == NULL)
	{
		return;
	}

	lv_obj_add_flag(context->about_panel, LV_OBJ_FLAG_HIDDEN);
	context->about_visible = 0U;

	if (Page_GetCurrent() == GUI_PAGE_SETTINGS)
	{
		GUI_Settings_RebuildFocus(context, context->about_btn);
		GUI_Settings_WaitInputRelease();
	}
}

static void GUI_Settings_UpdateAboutText(GUI_SettingsContext_t *context)
{
	u32 cpuid;
	u8 mid = 0U;
	u16 did = 0U;
	W25Q64_StatusTypeDef status;

	if (context == NULL || context->about_label == NULL)
	{
		return;
	}

	cpuid = SCB->CPUID;
	status = W25Q64_ReadID(&mid, &did);
	if (status == W25Q64_OK)
	{
		(void)snprintf(context->about_text, sizeof(context->about_text),
		               "Star Gamer\n"
		               "Author: ZLK\n"
		               "MCU/LCD: STM32F407ZGT6 / ST7789\n"
		               "Audio: PCM5102\n"
		               "Flash/EEPROM: W25Q64 / AT24C02\n"
		               "SRAM: IS62WV51216HBLL\n"
		               "CPUID: 0x%08lX\n"
		               "W25Q64 ID: 0x%02X%04X",
		               (unsigned long)cpuid,
		               (unsigned int)mid,
		               (unsigned int)did);
	}
	else
	{
		(void)snprintf(context->about_text, sizeof(context->about_text),
		               "Star Gamer\n"
		               "Author: ZLK\n"
		               "MCU/LCD: STM32F407ZGT6 / ST7789\n"
		               "Audio: PCM5102\n"
		               "Flash/EEPROM: W25Q64 / AT24C02\n"
		               "SRAM: IS62WV51216HBLL\n"
		               "CPUID: 0x%08lX\n"
		               "W25Q64 ID: read failed",
		               (unsigned long)cpuid);
	}

	lv_label_set_text(context->about_label, context->about_text);
}

static void GUI_Settings_UpdateFontState(GUI_SettingsContext_t *context)
{
	if (context == NULL || context->font_install_btn == NULL || context->font_uninstall_btn == NULL)
	{
		return;
	}

	if (GUI_Font_IsInstalled() != 0U)
	{
		GUI_Settings_SetListButtonEnabled(context->font_install_btn, 0U);
		GUI_Settings_SetListButtonEnabled(context->font_uninstall_btn, 1U);
		GUI_Settings_SetStatus(context, "Font already installed");
		context->first_item_btn = context->font_uninstall_btn;
	}
	else
	{
		GUI_Settings_SetListButtonEnabled(context->font_install_btn, 1U);
		GUI_Settings_SetListButtonEnabled(context->font_uninstall_btn, 0U);
		context->first_item_btn = context->font_install_btn;
	}
}

static void GUI_Settings_SetListButtonEnabled(lv_obj_t *btn, u8 enabled)
{
	if (btn == NULL)
	{
		return;
	}

	if (enabled != 0U)
	{
		lv_obj_clear_state(btn, LV_STATE_DISABLED);
		lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
	}
	else
	{
		lv_obj_add_state(btn, LV_STATE_DISABLED);
		lv_obj_clear_flag(btn, LV_OBJ_FLAG_CLICKABLE);
	}
}

static void GUI_Settings_SetStatus(GUI_SettingsContext_t *context, const char *text)
{
	if (context == NULL || context->status_label == NULL)
	{
		return;
	}

	lv_label_set_text(context->status_label, (text != NULL) ? text : "");
}

static void GUI_Settings_RebuildFocus(GUI_SettingsContext_t *context, lv_obj_t *focus_obj)
{
	lv_group_t *group;
	lv_obj_t *first_obj = NULL;

	if (context == NULL || context->screen == NULL)
	{
		return;
	}

	group = lv_group_get_default();
	if (group == NULL)
	{
		return;
	}

	lv_group_remove_all_objs(group);
	lv_group_set_editing(group, false);
	lv_group_set_wrap(group, true);

	if (context->about_visible != 0U)
	{
		if (context->about_close_btn != NULL)
		{
			lv_group_add_obj(group, context->about_close_btn);
			first_obj = context->about_close_btn;
		}
	}
	else
	{
		GUI_Settings_AddFocusableChildren(group, context->screen, &first_obj);
	}

	if (focus_obj != NULL && lv_obj_get_group(focus_obj) == group)
	{
		lv_group_focus_obj(focus_obj);
	}
	else if (first_obj != NULL)
	{
		lv_group_focus_obj(first_obj);
	}
}

static void GUI_Settings_AddFocusableChildren(lv_group_t *group, lv_obj_t *parent, lv_obj_t **first_obj)
{
	u32 i;
	u32 child_count;

	if (group == NULL || parent == NULL)
	{
		return;
	}

	if (lv_obj_has_flag(parent, LV_OBJ_FLAG_HIDDEN))
	{
		return;
	}

	child_count = lv_obj_get_child_cnt(parent);
	for (i = 0U; i < child_count; i++)
	{
		lv_obj_t *child = lv_obj_get_child(parent, (s32)i);

		if (lv_obj_has_flag(child, LV_OBJ_FLAG_HIDDEN))
		{
			continue;
		}

		if (lv_obj_is_group_def(child) && !lv_obj_has_state(child, LV_STATE_DISABLED))
		{
			lv_group_add_obj(group, child);
			if (first_obj != NULL && *first_obj == NULL)
			{
				*first_obj = child;
			}
		}

		GUI_Settings_AddFocusableChildren(group, child, first_obj);
	}
}
