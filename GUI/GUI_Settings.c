#include "GUI_Settings.h"
#include "GUI_Font.h"
#include "GUI_Theme.h"
#include "GUI_Message.h"
#include "IAP.h"
#include "System_Status.h"
#include "usb_device.h"
#include "w25q64.h"

#include <stdio.h>
#include <string.h>

#define GUI_SETTINGS_SCREEN_W        320
#define GUI_SETTINGS_LIST_X          10
#define GUI_SETTINGS_LIST_Y          46
#define GUI_SETTINGS_LIST_W          300
#define GUI_SETTINGS_LIST_H          162
#define GUI_SETTINGS_ROW_H           30
#define GUI_SETTINGS_ROW_ICON_X      12
#define GUI_SETTINGS_ROW_LABEL_X     38
#define GUI_SETTINGS_ROW_VALUE_PAD   12
#define GUI_SETTINGS_STATUS_Y        214
#define GUI_SETTINGS_SWITCH_X        236
#define GUI_SETTINGS_SWITCH_Y        4
#define GUI_SETTINGS_SWITCH_W        46
#define GUI_SETTINGS_SWITCH_H        22
#define GUI_SETTINGS_SWITCH_TOGGLE_KEY LV_KEY_ENTER
#define GUI_SETTINGS_ABOUT_X         10
#define GUI_SETTINGS_ABOUT_Y         46
#define GUI_SETTINGS_ABOUT_W         300
#define GUI_SETTINGS_ABOUT_H         186
#define GUI_SETTINGS_ABOUT_LABEL_W   280
#define GUI_SETTINGS_CLOSE_X         262
#define GUI_SETTINGS_CLOSE_Y         4
#define GUI_SETTINGS_TEXT_LEN        256U
#define GUI_SETTINGS_DEBUG_ITEM_COUNT 10U
#define GUI_SETTINGS_DEBUG_LIST_X    6
#define GUI_SETTINGS_DEBUG_LIST_Y    40
#define GUI_SETTINGS_DEBUG_LIST_W    288
#define GUI_SETTINGS_DEBUG_LIST_H    142
#define GUI_SETTINGS_DEBUG_SWITCH_X  226
#define GUI_SETTINGS_MSG_TEST_COUNT  3U

/* UTF-8 bytes for the CJK word meaning "test"; written as escapes so the
 * source stays encoding-independent (the XBF font renders it on device). */
#define GUI_SETTINGS_MSG_TEST_TEXT  "\xE6\xB5\x8B\xE8\xAF\x95"

typedef enum
{
	GUI_SETTINGS_ACTION_FONT_INSTALL = 0,
	GUI_SETTINGS_ACTION_FONT_UNINSTALL,
	GUI_SETTINGS_ACTION_UPDATE,
	GUI_SETTINGS_ACTION_DEBUG,
	GUI_SETTINGS_ACTION_ABOUT
} GUI_SettingsAction_t;

typedef enum
{
	GUI_SETTINGS_DEBUG_MASTER = 0,
	GUI_SETTINGS_DEBUG_SHELL,
	GUI_SETTINGS_DEBUG_AUDIO,
	GUI_SETTINGS_DEBUG_VIDEO,
	GUI_SETTINGS_DEBUG_LIBJPEG,
	GUI_SETTINGS_DEBUG_FATFS,
	GUI_SETTINGS_DEBUG_MEMORY_DUMP,
	GUI_SETTINGS_DEBUG_DUMP_CCM,
	GUI_SETTINGS_DEBUG_DUMP_MAIN,
	GUI_SETTINGS_DEBUG_DUMP_SRAMEX
} GUI_SettingsDebugItemId_t;

typedef struct
{
	GUI_SettingsAction_t action;
	const char *symbol;
	const char *text;
} GUI_SettingsItem_t;

typedef struct
{
	GUI_SettingsDebugItemId_t id;
	const char *symbol;
	const char *text;
} GUI_SettingsDebugItem_t;

typedef struct
{
	GUI_Message_Kind_t kind;
	const char *symbol;
	const char *text;
} GUI_SettingsMsgTestItem_t;

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
	lv_obj_t *debug_panel;
	lv_obj_t *debug_list;
	lv_obj_t *debug_close_btn;
	lv_obj_t *debug_btns[GUI_SETTINGS_DEBUG_ITEM_COUNT];
	lv_obj_t *debug_switches[GUI_SETTINGS_DEBUG_ITEM_COUNT];
	lv_obj_t *font_install_btn;
	lv_obj_t *font_uninstall_btn;
	lv_obj_t *theme_btn;
	lv_obj_t *theme_switch;
	lv_obj_t *usb_msc_btn;
	lv_obj_t *usb_msc_switch;
	lv_obj_t *clock_btn;
	lv_obj_t *clock_value;
	lv_obj_t *axf_stack_btn;
	lv_obj_t *axf_stack_value;
	lv_obj_t *toast_duration_btn;
	lv_obj_t *toast_duration_value;
	lv_obj_t *msg_test_btns[GUI_SETTINGS_MSG_TEST_COUNT];
	lv_obj_t *about_btn;
	lv_obj_t *debug_btn;
	lv_obj_t *first_item_btn;
	char about_text[GUI_SETTINGS_TEXT_LEN];
	u8 about_visible;
	u8 debug_visible;
} GUI_SettingsContext_t;

static int GUI_Settings_PageBuild(Page_Struct_t *page);
static int GUI_Settings_PageLoaded(Page_Struct_t *page, void *user_data);
static void GUI_Settings_ApplyScreenBase(lv_obj_t *screen);
static void GUI_Settings_CreateHeader(GUI_SettingsContext_t *context);
static void GUI_Settings_ApplyRowIconFont(lv_obj_t *row);
static void GUI_Settings_CreateList(GUI_SettingsContext_t *context);
static void GUI_Settings_CreateThemeSwitch(GUI_SettingsContext_t *context);
static void GUI_Settings_CreateUsbMscSwitch(GUI_SettingsContext_t *context);
static void GUI_Settings_CreateClockSelector(GUI_SettingsContext_t *context);
static void GUI_Settings_CreateAxfStackSelector(GUI_SettingsContext_t *context);
static void GUI_Settings_CreateToastDurationSelector(GUI_SettingsContext_t *context);
static void GUI_Settings_CreateMessageTestItems(GUI_SettingsContext_t *context);
static void GUI_Settings_AddListItem(GUI_SettingsContext_t *context, const GUI_SettingsItem_t *item);
static void GUI_Settings_CreateAboutPanel(GUI_SettingsContext_t *context);
static void GUI_Settings_CreateDebugPanel(GUI_SettingsContext_t *context);
static void GUI_Settings_AddDebugItem(GUI_SettingsContext_t *context, const GUI_SettingsDebugItem_t *item);
static void GUI_Settings_BackEvent(lv_event_t *event);
static void GUI_Settings_ItemEvent(lv_event_t *event);
static void GUI_Settings_ThemeEvent(lv_event_t *event);
static void GUI_Settings_UsbMscEvent(lv_event_t *event);
static void GUI_Settings_ClockEvent(lv_event_t *event);
static void GUI_Settings_AxfStackEvent(lv_event_t *event);
static void GUI_Settings_ToastDurationEvent(lv_event_t *event);
static void GUI_Settings_MessageTestEvent(lv_event_t *event);
static void GUI_Settings_AboutCloseEvent(lv_event_t *event);
static void GUI_Settings_DebugCloseEvent(lv_event_t *event);
static void GUI_Settings_DebugItemEvent(lv_event_t *event);
static void GUI_Settings_WaitInputRelease(void);
static u8 GUI_Settings_HandleKey(u32 key);
static u8 GUI_Settings_IsSwitchToggleKey(u32 key);
static void GUI_Settings_RunAction(GUI_SettingsContext_t *context, GUI_SettingsAction_t action);
static void GUI_Settings_FontInstallResult(GUI_Message_Result_t result, void *user_data);
static void GUI_Settings_FontUninstallResult(GUI_Message_Result_t result, void *user_data);
static void GUI_Settings_UpdateResult(GUI_Message_Result_t result, void *user_data);
static void GUI_Settings_UpdateProgress(u8 percent, void *context);
static void GUI_Settings_ShowAbout(GUI_SettingsContext_t *context);
static void GUI_Settings_HideAbout(GUI_SettingsContext_t *context);
static void GUI_Settings_ShowDebug(GUI_SettingsContext_t *context);
static void GUI_Settings_HideDebug(GUI_SettingsContext_t *context);
static void GUI_Settings_UpdateDebugSwitches(GUI_SettingsContext_t *context);
static void GUI_Settings_ToggleDebugItem(GUI_SettingsContext_t *context, GUI_SettingsDebugItemId_t id);
static u8 GUI_Settings_DebugItemValue(GUI_SettingsDebugItemId_t id);
static void GUI_Settings_UpdateAboutText(GUI_SettingsContext_t *context);
static void GUI_Settings_UpdateFontState(GUI_SettingsContext_t *context);
static void GUI_Settings_UpdateThemeSwitch(GUI_SettingsContext_t *context);
static void GUI_Settings_SetThemeLightEnabled(GUI_SettingsContext_t *context, u8 enabled);
static void GUI_Settings_ThemeReloadAsync(void *user_data);
static void GUI_Settings_UpdateUsbMscSwitch(GUI_SettingsContext_t *context);
static void GUI_Settings_SetUsbMscEnabled(GUI_SettingsContext_t *context, u8 enabled);
static void GUI_Settings_UpdateClock(GUI_SettingsContext_t *context);
static void GUI_Settings_SetClockProfile(GUI_SettingsContext_t *context, u8 profile);
static void GUI_Settings_UpdateAxfStack(GUI_SettingsContext_t *context);
static void GUI_Settings_SetAxfStackLevel(GUI_SettingsContext_t *context, u8 level);
static void GUI_Settings_UpdateToastDuration(GUI_SettingsContext_t *context);
static void GUI_Settings_SetToastDuration(GUI_SettingsContext_t *context, u8 seconds);
static u8 GUI_Settings_ToastDurationIndex(u8 seconds);
static void GUI_Settings_SetListButtonEnabled(lv_obj_t *btn, u8 enabled);
static void GUI_Settings_SetStatus(GUI_SettingsContext_t *context, const char *text);
static void GUI_Settings_RebuildFocus(GUI_SettingsContext_t *context, lv_obj_t *focus_obj);
static void GUI_Settings_AddFocusableChildren(lv_group_t *group, lv_obj_t *parent, lv_obj_t **first_obj);
static u8 GUI_Settings_IsInternalSwitch(lv_obj_t *object);

static GUI_SettingsContext_t s_settings;

static const char * const s_axf_stack_labels[SYSTEM_AXF_STACK_LEVEL_COUNT] =
{
	"4 KB", "8 KB", "12 KB", "16 KB", "24 KB"
};

static const char * const s_clock_labels[SYSTEM_CLOCK_PROFILE_COUNT] =
{
	"192 MHz", "216 MHz", "240 MHz"
};

static const GUI_SettingsItem_t s_setting_items[] =
{
	{GUI_SETTINGS_ACTION_FONT_INSTALL, GUI_ICON_FONT, "Install Font"},
	{GUI_SETTINGS_ACTION_FONT_UNINSTALL, GUI_ICON_DELETE, "Uninstall Font"},
	{GUI_SETTINGS_ACTION_UPDATE, GUI_ICON_DOWNLOAD, "Install Update"},
	{GUI_SETTINGS_ACTION_DEBUG, GUI_ICON_BUG, "Debug"},
	{GUI_SETTINGS_ACTION_ABOUT, GUI_ICON_INFO, "About"}
};

static const GUI_SettingsMsgTestItem_t s_msg_test_items[GUI_SETTINGS_MSG_TEST_COUNT] =
{
	{GUI_MESSAGE_KIND_TOAST, GUI_ICON_INFO, "Toast Test"},
	{GUI_MESSAGE_KIND_CONFIRM, GUI_ICON_CHECK_CIRCLE, "Confirm Test"},
	{GUI_MESSAGE_KIND_YESNO, GUI_ICON_MORE_HORIZ, "Yes / No Test"}
};

static const u8 s_toast_duration_presets[] = {1U, 2U, 3U, 5U, 10U};

#define GUI_SETTINGS_TOAST_DURATION_COUNT \
	((u8)(sizeof(s_toast_duration_presets) / sizeof(s_toast_duration_presets[0])))
#define GUI_SETTINGS_TOAST_DURATION_DEFAULT_INDEX 2U

static char s_toast_value_text[8];

static const GUI_SettingsDebugItem_t s_debug_items[GUI_SETTINGS_DEBUG_ITEM_COUNT] =
{
	{GUI_SETTINGS_DEBUG_MASTER, GUI_ICON_BUG, "Debug"},
	{GUI_SETTINGS_DEBUG_SHELL, GUI_ICON_TERMINAL, "Shell"},
	{GUI_SETTINGS_DEBUG_AUDIO, GUI_ICON_MUSIC, "Audio Log"},
	{GUI_SETTINGS_DEBUG_VIDEO, GUI_ICON_MOVIE, "Video Log"},
	{GUI_SETTINGS_DEBUG_LIBJPEG, GUI_ICON_IMAGE, "LibJPEG Log"},
	{GUI_SETTINGS_DEBUG_FATFS, GUI_ICON_SD_CARD, "FatFs Log"},
	{GUI_SETTINGS_DEBUG_MEMORY_DUMP, GUI_ICON_STORAGE, "Memory Dump"},
	{GUI_SETTINGS_DEBUG_DUMP_CCM, GUI_ICON_MEMORY, "Dump CCM"},
	{GUI_SETTINGS_DEBUG_DUMP_MAIN, GUI_ICON_MEMORY, "Dump Main SRAM"},
	{GUI_SETTINGS_DEBUG_DUMP_SRAMEX, GUI_ICON_MEMORY, "Dump SRAMEX"}
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

	return Page_Show_Anim(GUI_PAGE_SETTINGS, LV_SCR_LOAD_ANIM_MOVE_LEFT, GUI_M3_MOTION_PAGE, 0U);
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
	GUI_Settings_CreateDebugPanel(&s_settings);
	GUI_Settings_SetStatus(&s_settings, "Ready");
	GUI_Settings_UpdateThemeSwitch(&s_settings);
	GUI_Settings_UpdateUsbMscSwitch(&s_settings);
	GUI_Settings_UpdateClock(&s_settings);
	GUI_Settings_UpdateAxfStack(&s_settings);
	GUI_Settings_UpdateToastDuration(&s_settings);
	GUI_Settings_UpdateDebugSwitches(&s_settings);
	GUI_Settings_UpdateFontState(&s_settings);

	return SYSTEM_STATUS_OK;
}

static int GUI_Settings_PageLoaded(Page_Struct_t *page, void *user_data)
{
	(void)page;
	(void)user_data;

	GUI_Settings_UpdateFontState(&s_settings);
	GUI_Settings_UpdateThemeSwitch(&s_settings);
	GUI_Settings_UpdateUsbMscSwitch(&s_settings);
	GUI_Settings_UpdateClock(&s_settings);
	GUI_Settings_UpdateAxfStack(&s_settings);
	GUI_Settings_UpdateToastDuration(&s_settings);
	GUI_Settings_UpdateDebugSwitches(&s_settings);
	GUI_Settings_RebuildFocus(&s_settings, s_settings.first_item_btn);
	GUI_Settings_WaitInputRelease();
	lv_obj_invalidate(s_settings.screen);

	return SYSTEM_STATUS_OK;
}

static void GUI_Settings_ApplyScreenBase(lv_obj_t *screen)
{
	GUI_M3_ApplyScreen(screen);
}

static void GUI_Settings_CreateHeader(GUI_SettingsContext_t *context)
{
	lv_obj_t *bar;

	bar = GUI_M3_CreateTopAppBar(context->screen, "Settings");
	context->back_btn = GUI_M3_AppBarSetNav(bar, GUI_ICON_ARROW_BACK,
	                                        GUI_Settings_BackEvent, &s_settings);
}

/* Rows built by lv_list_add_btn render the leading glyph through an lv_img
 * symbol child, so that child needs the Material Symbols font. The color is
 * left unset on purpose: it is inherited from the row and therefore follows
 * the focused / disabled state layers of the row itself. */
static void GUI_Settings_ApplyRowIconFont(lv_obj_t *row)
{
	lv_obj_t *icon;

	if (row == NULL || lv_obj_get_child_cnt(row) == 0U)
	{
		return;
	}

	icon = lv_obj_get_child(row, 0);
	if (icon != NULL)
	{
		lv_obj_set_style_text_font(icon, GUI_M3_IconFont(), 0);
	}
}

static void GUI_Settings_CreateList(GUI_SettingsContext_t *context)
{
	u8 i;

	context->list = lv_list_create(context->screen);
	lv_obj_set_size(context->list, GUI_SETTINGS_LIST_W, GUI_SETTINGS_LIST_H);
	lv_obj_set_pos(context->list, GUI_SETTINGS_LIST_X, GUI_SETTINGS_LIST_Y);
	GUI_M3_StyleList(context->list);
	lv_obj_set_scrollbar_mode(context->list, LV_SCROLLBAR_MODE_OFF);

	GUI_Settings_CreateThemeSwitch(context);
	GUI_Settings_CreateUsbMscSwitch(context);
	GUI_Settings_CreateClockSelector(context);
	GUI_Settings_CreateAxfStackSelector(context);
	GUI_Settings_CreateToastDurationSelector(context);
	GUI_Settings_CreateMessageTestItems(context);

	for (i = 0U; i < (u8)(sizeof(s_setting_items) / sizeof(s_setting_items[0])); i++)
	{
		GUI_Settings_AddListItem(context, &s_setting_items[i]);
	}

	context->status_label = GUI_M3_CreateLabel(context->screen, "", GUI_M3_TYPE_LABEL,
	                                           GUI_THEME_COLOR_ON_SURFACE_VARIANT);
	if (context->status_label != NULL)
	{
		lv_label_set_long_mode(context->status_label, LV_LABEL_LONG_DOT);
		lv_obj_set_width(context->status_label, GUI_SETTINGS_SCREEN_W - 24);
		lv_obj_set_pos(context->status_label, 12, GUI_SETTINGS_STATUS_Y);
	}
}

static void GUI_Settings_CreateThemeSwitch(GUI_SettingsContext_t *context)
{
	lv_obj_t *icon;
	lv_obj_t *label;

	if (context == NULL || context->list == NULL)
	{
		return;
	}

	context->theme_btn = lv_btn_create(context->list);
	lv_obj_clear_flag(context->theme_btn, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(context->theme_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
	lv_obj_set_size(context->theme_btn, GUI_SETTINGS_LIST_W - 8, GUI_SETTINGS_ROW_H);
	GUI_M3_StyleListItem(context->theme_btn);
	lv_obj_set_style_pad_all(context->theme_btn, 0, 0);
	lv_obj_add_event_cb(context->theme_btn, GUI_Settings_ThemeEvent, LV_EVENT_KEY, context);

	icon = GUI_M3_CreateIconLabel(context->theme_btn, GUI_ICON_LIGHT_MODE,
	                              GUI_THEME_COLOR_ON_SURFACE_VARIANT, 0U);
	if (icon != NULL)
	{
		lv_obj_align(icon, LV_ALIGN_LEFT_MID, GUI_SETTINGS_ROW_ICON_X, 0);
	}

	label = GUI_M3_CreateLabel(context->theme_btn, "Light Theme", GUI_M3_TYPE_BODY,
	                           GUI_THEME_COLOR_ON_SURFACE);
	if (label != NULL)
	{
		lv_obj_align(label, LV_ALIGN_LEFT_MID, GUI_SETTINGS_ROW_LABEL_X, 0);
	}

	context->theme_switch = lv_switch_create(context->theme_btn);
	lv_obj_clear_flag(context->theme_switch, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(context->theme_switch, GUI_SETTINGS_SWITCH_W, GUI_SETTINGS_SWITCH_H);
	lv_obj_set_pos(context->theme_switch, GUI_SETTINGS_SWITCH_X, GUI_SETTINGS_SWITCH_Y);
	GUI_M3_StyleSwitch(context->theme_switch);

	GUI_Settings_UpdateThemeSwitch(context);

	if (context->first_item_btn == NULL)
	{
		context->first_item_btn = context->theme_btn;
	}
}

static void GUI_Settings_CreateUsbMscSwitch(GUI_SettingsContext_t *context)
{
	lv_obj_t *icon;
	lv_obj_t *label;

	if (context == NULL || context->list == NULL)
	{
		return;
	}

	context->usb_msc_btn = lv_btn_create(context->list);
	lv_obj_clear_flag(context->usb_msc_btn, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(context->usb_msc_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
	lv_obj_set_size(context->usb_msc_btn, GUI_SETTINGS_LIST_W - 8, GUI_SETTINGS_ROW_H);
	GUI_M3_StyleListItem(context->usb_msc_btn);
	lv_obj_set_style_pad_all(context->usb_msc_btn, 0, 0);
	lv_obj_add_event_cb(context->usb_msc_btn, GUI_Settings_UsbMscEvent, LV_EVENT_KEY, context);

	icon = GUI_M3_CreateIconLabel(context->usb_msc_btn, GUI_ICON_USB,
	                              GUI_THEME_COLOR_ON_SURFACE_VARIANT, 0U);
	if (icon != NULL)
	{
		lv_obj_align(icon, LV_ALIGN_LEFT_MID, GUI_SETTINGS_ROW_ICON_X, 0);
	}

	label = GUI_M3_CreateLabel(context->usb_msc_btn, "USB MSC", GUI_M3_TYPE_BODY,
	                           GUI_THEME_COLOR_ON_SURFACE);
	if (label != NULL)
	{
		lv_obj_align(label, LV_ALIGN_LEFT_MID, GUI_SETTINGS_ROW_LABEL_X, 0);
	}

	context->usb_msc_switch = lv_switch_create(context->usb_msc_btn);
	lv_obj_clear_flag(context->usb_msc_switch, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(context->usb_msc_switch, GUI_SETTINGS_SWITCH_W, GUI_SETTINGS_SWITCH_H);
	lv_obj_set_pos(context->usb_msc_switch, GUI_SETTINGS_SWITCH_X, GUI_SETTINGS_SWITCH_Y);
	GUI_M3_StyleSwitch(context->usb_msc_switch);

	GUI_Settings_UpdateUsbMscSwitch(context);

	if (context->first_item_btn == NULL)
	{
		context->first_item_btn = context->usb_msc_btn;
	}
}

static void GUI_Settings_CreateClockSelector(GUI_SettingsContext_t *context)
{
	lv_obj_t *icon;
	lv_obj_t *label;

	if (context == NULL || context->list == NULL)
	{
		return;
	}

	context->clock_btn = lv_btn_create(context->list);
	lv_obj_clear_flag(context->clock_btn, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(context->clock_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
	lv_obj_set_size(context->clock_btn, GUI_SETTINGS_LIST_W - 8, GUI_SETTINGS_ROW_H);
	GUI_M3_StyleListItem(context->clock_btn);
	lv_obj_set_style_pad_all(context->clock_btn, 0, 0);
	lv_obj_add_event_cb(context->clock_btn, GUI_Settings_ClockEvent,
	                    LV_EVENT_CLICKED, context);
	lv_obj_add_event_cb(context->clock_btn, GUI_Settings_ClockEvent,
	                    LV_EVENT_KEY, context);

	icon = GUI_M3_CreateIconLabel(context->clock_btn, GUI_ICON_SPEED,
	                              GUI_THEME_COLOR_ON_SURFACE_VARIANT, 0U);
	if (icon != NULL)
	{
		lv_obj_align(icon, LV_ALIGN_LEFT_MID, GUI_SETTINGS_ROW_ICON_X, 0);
	}

	label = GUI_M3_CreateLabel(context->clock_btn, "CPU Clock", GUI_M3_TYPE_BODY,
	                           GUI_THEME_COLOR_ON_SURFACE);
	if (label != NULL)
	{
		lv_obj_align(label, LV_ALIGN_LEFT_MID, GUI_SETTINGS_ROW_LABEL_X, 0);
	}

	context->clock_value = GUI_M3_CreateLabel(context->clock_btn, "", GUI_M3_TYPE_LABEL,
	                                          GUI_THEME_COLOR_ON_SURFACE_VARIANT);
	if (context->clock_value != NULL)
	{
		lv_label_set_long_mode(context->clock_value, LV_LABEL_LONG_CLIP);
		lv_obj_set_width(context->clock_value, 82);
		lv_obj_set_style_text_align(context->clock_value, LV_TEXT_ALIGN_RIGHT, 0);
		lv_obj_align(context->clock_value, LV_ALIGN_RIGHT_MID,
		             -GUI_SETTINGS_ROW_VALUE_PAD, 0);
	}
	GUI_Settings_UpdateClock(context);
}

static void GUI_Settings_CreateAxfStackSelector(GUI_SettingsContext_t *context)
{
	lv_obj_t *icon;
	lv_obj_t *label;

	if (context == NULL || context->list == NULL)
	{
		return;
	}

	context->axf_stack_btn = lv_btn_create(context->list);
	lv_obj_clear_flag(context->axf_stack_btn, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(context->axf_stack_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
	lv_obj_set_size(context->axf_stack_btn, GUI_SETTINGS_LIST_W - 8,
	                GUI_SETTINGS_ROW_H);
	GUI_M3_StyleListItem(context->axf_stack_btn);
	lv_obj_set_style_pad_all(context->axf_stack_btn, 0, 0);
	lv_obj_add_event_cb(context->axf_stack_btn, GUI_Settings_AxfStackEvent,
	                    LV_EVENT_CLICKED, context);
	lv_obj_add_event_cb(context->axf_stack_btn, GUI_Settings_AxfStackEvent,
	                    LV_EVENT_KEY, context);

	icon = GUI_M3_CreateIconLabel(context->axf_stack_btn, GUI_ICON_MEMORY,
	                              GUI_THEME_COLOR_ON_SURFACE_VARIANT, 0U);
	if (icon != NULL)
	{
		lv_obj_align(icon, LV_ALIGN_LEFT_MID, GUI_SETTINGS_ROW_ICON_X, 0);
	}

	label = GUI_M3_CreateLabel(context->axf_stack_btn, "AXF Stack", GUI_M3_TYPE_BODY,
	                           GUI_THEME_COLOR_ON_SURFACE);
	if (label != NULL)
	{
		lv_obj_align(label, LV_ALIGN_LEFT_MID, GUI_SETTINGS_ROW_LABEL_X, 0);
	}

	context->axf_stack_value = GUI_M3_CreateLabel(context->axf_stack_btn, "",
	                                              GUI_M3_TYPE_LABEL,
	                                              GUI_THEME_COLOR_ON_SURFACE_VARIANT);
	if (context->axf_stack_value != NULL)
	{
		lv_label_set_long_mode(context->axf_stack_value, LV_LABEL_LONG_CLIP);
		lv_obj_set_width(context->axf_stack_value, 58);
		lv_obj_set_style_text_align(context->axf_stack_value, LV_TEXT_ALIGN_RIGHT, 0);
		lv_obj_align(context->axf_stack_value, LV_ALIGN_RIGHT_MID,
		             -GUI_SETTINGS_ROW_VALUE_PAD, 0);
	}
	GUI_Settings_UpdateAxfStack(context);
}

static void GUI_Settings_CreateToastDurationSelector(GUI_SettingsContext_t *context)
{
	lv_obj_t *icon;
	lv_obj_t *label;

	if (context == NULL || context->list == NULL)
	{
		return;
	}

	context->toast_duration_btn = lv_btn_create(context->list);
	lv_obj_clear_flag(context->toast_duration_btn, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(context->toast_duration_btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
	lv_obj_set_size(context->toast_duration_btn, GUI_SETTINGS_LIST_W - 8,
	                GUI_SETTINGS_ROW_H);
	GUI_M3_StyleListItem(context->toast_duration_btn);
	lv_obj_set_style_pad_all(context->toast_duration_btn, 0, 0);
	lv_obj_add_event_cb(context->toast_duration_btn, GUI_Settings_ToastDurationEvent,
	                    LV_EVENT_CLICKED, NULL);
	lv_obj_add_event_cb(context->toast_duration_btn, GUI_Settings_ToastDurationEvent,
	                    LV_EVENT_KEY, NULL);

	icon = GUI_M3_CreateIconLabel(context->toast_duration_btn, GUI_ICON_TUNE,
	                              GUI_THEME_COLOR_ON_SURFACE_VARIANT, 0U);
	if (icon != NULL)
	{
		lv_obj_align(icon, LV_ALIGN_LEFT_MID, GUI_SETTINGS_ROW_ICON_X, 0);
	}

	label = GUI_M3_CreateLabel(context->toast_duration_btn, "Toast Duration",
	                           GUI_M3_TYPE_BODY, GUI_THEME_COLOR_ON_SURFACE);
	if (label != NULL)
	{
		lv_obj_align(label, LV_ALIGN_LEFT_MID, GUI_SETTINGS_ROW_LABEL_X, 0);
	}

	context->toast_duration_value = GUI_M3_CreateLabel(context->toast_duration_btn, "",
	                                                   GUI_M3_TYPE_LABEL,
	                                                   GUI_THEME_COLOR_ON_SURFACE_VARIANT);
	if (context->toast_duration_value != NULL)
	{
		lv_label_set_long_mode(context->toast_duration_value, LV_LABEL_LONG_CLIP);
		lv_obj_set_width(context->toast_duration_value, 58);
		lv_obj_set_style_text_align(context->toast_duration_value, LV_TEXT_ALIGN_RIGHT, 0);
		lv_obj_align(context->toast_duration_value, LV_ALIGN_RIGHT_MID,
		             -GUI_SETTINGS_ROW_VALUE_PAD, 0);
	}
	GUI_Settings_UpdateToastDuration(context);
}

static void GUI_Settings_CreateMessageTestItems(GUI_SettingsContext_t *context)
{
	u8 i;

	if (context == NULL || context->list == NULL)
	{
		return;
	}

	for (i = 0U; i < GUI_SETTINGS_MSG_TEST_COUNT; i++)
	{
		lv_obj_t *btn = lv_list_add_btn(context->list, s_msg_test_items[i].symbol,
		                                s_msg_test_items[i].text);
		lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
		lv_obj_set_height(btn, GUI_SETTINGS_ROW_H);
		lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER,
		                      LV_FLEX_ALIGN_CENTER);
		GUI_M3_StyleListItem(btn);
		GUI_Settings_ApplyRowIconFont(btn);
		lv_obj_add_event_cb(btn, GUI_Settings_MessageTestEvent, LV_EVENT_CLICKED,
		                    (void *)&s_msg_test_items[i]);
		lv_obj_add_event_cb(btn, GUI_Settings_MessageTestEvent, LV_EVENT_KEY,
		                    (void *)&s_msg_test_items[i]);
		context->msg_test_btns[i] = btn;
	}
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
	lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
	lv_obj_set_height(btn, GUI_SETTINGS_ROW_H);
	lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	GUI_M3_StyleListItem(btn);
	GUI_Settings_ApplyRowIconFont(btn);
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
	else if (item->action == GUI_SETTINGS_ACTION_DEBUG)
	{
		context->debug_btn = btn;
	}
}

static void GUI_Settings_CreateAboutPanel(GUI_SettingsContext_t *context)
{
	lv_obj_t *title;

	context->about_panel = GUI_M3_CreateCard(context->screen, GUI_M3_CARD_FILLED);
	lv_obj_set_size(context->about_panel, GUI_SETTINGS_ABOUT_W, GUI_SETTINGS_ABOUT_H);
	lv_obj_set_pos(context->about_panel, GUI_SETTINGS_ABOUT_X, GUI_SETTINGS_ABOUT_Y);
	/* The card children keep absolute coordinates, so drop the card padding. */
	lv_obj_set_style_pad_all(context->about_panel, 0, 0);

	title = GUI_M3_CreateLabel(context->about_panel, "About", GUI_M3_TYPE_TITLE,
	                           GUI_THEME_COLOR_ON_SURFACE);
	if (title != NULL)
	{
		lv_obj_set_pos(title, 12, 10);
	}

	context->about_close_btn = GUI_M3_CreateIconButton(context->about_panel, GUI_ICON_CLOSE,
	                                                   GUI_M3_BUTTON_ICON);
	if (context->about_close_btn != NULL)
	{
		lv_obj_set_pos(context->about_close_btn, GUI_SETTINGS_CLOSE_X, GUI_SETTINGS_CLOSE_Y);
		lv_obj_add_event_cb(context->about_close_btn, GUI_Settings_AboutCloseEvent, LV_EVENT_CLICKED, context);
		lv_obj_add_event_cb(context->about_close_btn, GUI_Settings_AboutCloseEvent, LV_EVENT_KEY, context);
	}

	context->about_label = lv_label_create(context->about_panel);
	lv_label_set_long_mode(context->about_label, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(context->about_label, GUI_SETTINGS_ABOUT_LABEL_W);
	lv_obj_set_pos(context->about_label, 12, 38);
	lv_obj_set_style_text_color(context->about_label,
	                            GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE_VARIANT), 0);
	lv_obj_set_style_text_line_space(context->about_label, 1, 0);

	GUI_Settings_HideAbout(context);
}

static void GUI_Settings_CreateDebugPanel(GUI_SettingsContext_t *context)
{
	lv_obj_t *title;
	u8 index;

	context->debug_panel = GUI_M3_CreateCard(context->screen, GUI_M3_CARD_FILLED);
	lv_obj_set_size(context->debug_panel, GUI_SETTINGS_ABOUT_W, GUI_SETTINGS_ABOUT_H);
	lv_obj_set_pos(context->debug_panel, GUI_SETTINGS_ABOUT_X, GUI_SETTINGS_ABOUT_Y);
	/* The card children keep absolute coordinates, so drop the card padding. */
	lv_obj_set_style_pad_all(context->debug_panel, 0, 0);

	title = GUI_M3_CreateLabel(context->debug_panel, "Debug", GUI_M3_TYPE_TITLE,
	                           GUI_THEME_COLOR_ON_SURFACE);
	if (title != NULL)
	{
		lv_obj_set_pos(title, 12, 10);
	}

	context->debug_close_btn = GUI_M3_CreateIconButton(context->debug_panel, GUI_ICON_CLOSE,
	                                                   GUI_M3_BUTTON_ICON);
	if (context->debug_close_btn != NULL)
	{
		lv_obj_set_pos(context->debug_close_btn, GUI_SETTINGS_CLOSE_X, GUI_SETTINGS_CLOSE_Y);
		lv_obj_add_event_cb(context->debug_close_btn, GUI_Settings_DebugCloseEvent, LV_EVENT_CLICKED, context);
		lv_obj_add_event_cb(context->debug_close_btn, GUI_Settings_DebugCloseEvent, LV_EVENT_KEY, context);
	}

	context->debug_list = lv_list_create(context->debug_panel);
	lv_obj_set_size(context->debug_list, GUI_SETTINGS_DEBUG_LIST_W, GUI_SETTINGS_DEBUG_LIST_H);
	lv_obj_set_pos(context->debug_list, GUI_SETTINGS_DEBUG_LIST_X, GUI_SETTINGS_DEBUG_LIST_Y);
	GUI_M3_StyleList(context->debug_list);
	lv_obj_set_scrollbar_mode(context->debug_list, LV_SCROLLBAR_MODE_AUTO);

	for (index = 0U; index < GUI_SETTINGS_DEBUG_ITEM_COUNT; index++)
	{
		GUI_Settings_AddDebugItem(context, &s_debug_items[index]);
	}
	GUI_Settings_UpdateDebugSwitches(context);
	GUI_Settings_HideDebug(context);
}

static void GUI_Settings_AddDebugItem(GUI_SettingsContext_t *context,
	const GUI_SettingsDebugItem_t *item)
{
	lv_obj_t *btn;
	lv_obj_t *toggle;
	u32 index;

	if (context == NULL || context->debug_list == NULL || item == NULL)
	{
		return;
	}
	index = (u32)item->id;
	if (index >= GUI_SETTINGS_DEBUG_ITEM_COUNT)
	{
		return;
	}

	btn = lv_list_add_btn(context->debug_list, item->symbol, item->text);
	lv_obj_add_flag(btn, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
	lv_obj_set_height(btn, GUI_SETTINGS_ROW_H);
	lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	GUI_M3_StyleListItem(btn);
	GUI_Settings_ApplyRowIconFont(btn);
	lv_obj_add_event_cb(btn, GUI_Settings_DebugItemEvent, LV_EVENT_CLICKED, (void *)item);
	lv_obj_add_event_cb(btn, GUI_Settings_DebugItemEvent, LV_EVENT_KEY, (void *)item);

	toggle = lv_switch_create(btn);
	lv_obj_clear_flag(toggle, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(toggle, GUI_SETTINGS_SWITCH_W, GUI_SETTINGS_SWITCH_H);
	lv_obj_set_pos(toggle, GUI_SETTINGS_DEBUG_SWITCH_X, GUI_SETTINGS_SWITCH_Y);
	GUI_M3_StyleSwitch(toggle);

	context->debug_btns[index] = btn;
	context->debug_switches[index] = toggle;
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
		else if (context->debug_visible != 0U)
		{
			GUI_Settings_HideDebug(context);
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
			if (context->about_visible != 0U)
			{
				GUI_Settings_HideAbout(context);
			}
			else if (context->debug_visible != 0U)
			{
				GUI_Settings_HideDebug(context);
			}
			else
			{
				(void)Page_Back();
			}
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
	if (s_settings.debug_visible != 0U)
	{
		if (code == LV_EVENT_KEY)
		{
			key = lv_event_get_key(event);
			if (key == LV_KEY_ESC || key == LV_KEY_LEFT)
			{
				GUI_Settings_HideDebug(&s_settings);
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

static void GUI_Settings_ThemeEvent(lv_event_t *event)
{
	GUI_SettingsContext_t *context = (GUI_SettingsContext_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;
	u8 enabled;

	if (context == NULL)
	{
		return;
	}

	if (context->about_visible != 0U || context->debug_visible != 0U)
	{
		return;
	}

	if (code == LV_EVENT_KEY)
	{
		key = lv_event_get_key(event);
		if (GUI_Settings_IsSwitchToggleKey(key) != 0U || key == LV_KEY_RIGHT)
		{
			enabled = (System_Settings_ThemeLightEnabled() == 0U) ? 1U : 0U;
			GUI_Settings_SetThemeLightEnabled(context, enabled);
		}
		else if (key == LV_KEY_ESC || key == LV_KEY_LEFT)
		{
			(void)Page_Back();
		}
		else
		{
			(void)GUI_Settings_HandleKey(key);
		}
	}
}

static void GUI_Settings_UsbMscEvent(lv_event_t *event)
{
	GUI_SettingsContext_t *context = (GUI_SettingsContext_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;
	u8 enabled;

	if (context == NULL)
	{
		return;
	}

	if (context->about_visible != 0U || context->debug_visible != 0U)
	{
		return;
	}

	if (code == LV_EVENT_KEY)
	{
		key = lv_event_get_key(event);
		if (GUI_Settings_IsSwitchToggleKey(key) != 0U)
		{
			enabled = (System_Settings_UsbMscEnabled() == 0U) ? 1U : 0U;
			GUI_Settings_SetUsbMscEnabled(context, enabled);
		}
		else if (key == LV_KEY_ESC)
		{
			(void)Page_Back();
		}
		else
		{
			(void)GUI_Settings_HandleKey(key);
		}
	}
}

static void GUI_Settings_ClockEvent(lv_event_t *event)
{
	GUI_SettingsContext_t *context =
		(GUI_SettingsContext_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;
	u8 profile;

	if (context == NULL || context->about_visible != 0U ||
	    context->debug_visible != 0U)
	{
		return;
	}

	profile = System_Settings_ClockProfile();
	if (code == LV_EVENT_CLICKED)
	{
		profile = (u8)((profile + 1U) % SYSTEM_CLOCK_PROFILE_COUNT);
		GUI_Settings_SetClockProfile(context, profile);
		return;
	}
	if (code != LV_EVENT_KEY)
	{
		return;
	}

	key = lv_event_get_key(event);
	if (key == LV_KEY_RIGHT)
	{
		profile = (u8)((profile + 1U) % SYSTEM_CLOCK_PROFILE_COUNT);
		GUI_Settings_SetClockProfile(context, profile);
	}
	else if (key == LV_KEY_LEFT)
	{
		profile = (profile == 0U) ? (SYSTEM_CLOCK_PROFILE_COUNT - 1U) :
		        (u8)(profile - 1U);
		GUI_Settings_SetClockProfile(context, profile);
	}
	else if (key == LV_KEY_ESC)
	{
		(void)Page_Back();
	}
	else if (key != LV_KEY_ENTER)
	{
		(void)GUI_Settings_HandleKey(key);
	}
}

static void GUI_Settings_AxfStackEvent(lv_event_t *event)
{
	GUI_SettingsContext_t *context =
		(GUI_SettingsContext_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;
	u8 level;

	if (context == NULL || context->about_visible != 0U ||
	    context->debug_visible != 0U)
	{
		return;
	}

	level = System_Settings_AxfStackLevel();
	if (code == LV_EVENT_CLICKED)
	{
		level = (u8)((level + 1U) % SYSTEM_AXF_STACK_LEVEL_COUNT);
		GUI_Settings_SetAxfStackLevel(context, level);
		return;
	}
	if (code != LV_EVENT_KEY)
	{
		return;
	}

	key = lv_event_get_key(event);
	if (key == LV_KEY_RIGHT)
	{
		level = (u8)((level + 1U) % SYSTEM_AXF_STACK_LEVEL_COUNT);
		GUI_Settings_SetAxfStackLevel(context, level);
	}
	else if (key == LV_KEY_LEFT)
	{
		level = (level == 0U) ? (SYSTEM_AXF_STACK_LEVEL_COUNT - 1U) :
		        (u8)(level - 1U);
		GUI_Settings_SetAxfStackLevel(context, level);
	}
	else if (key == LV_KEY_ESC)
	{
		(void)Page_Back();
	}
	else if (key != LV_KEY_ENTER)
	{
		(void)GUI_Settings_HandleKey(key);
	}
}

static void GUI_Settings_ToastDurationEvent(lv_event_t *event)
{
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;
	u8 index;

	if (s_settings.about_visible != 0U || s_settings.debug_visible != 0U)
	{
		return;
	}

	index = GUI_Settings_ToastDurationIndex(System_Settings_ToastDurationSec());
	if (code == LV_EVENT_CLICKED)
	{
		index = (u8)((index + 1U) % GUI_SETTINGS_TOAST_DURATION_COUNT);
		GUI_Settings_SetToastDuration(&s_settings, s_toast_duration_presets[index]);
		return;
	}
	if (code != LV_EVENT_KEY)
	{
		return;
	}

	key = lv_event_get_key(event);
	if (key == LV_KEY_RIGHT)
	{
		index = (u8)((index + 1U) % GUI_SETTINGS_TOAST_DURATION_COUNT);
		GUI_Settings_SetToastDuration(&s_settings, s_toast_duration_presets[index]);
	}
	else if (key == LV_KEY_LEFT)
	{
		index = (index == 0U) ? (u8)(GUI_SETTINGS_TOAST_DURATION_COUNT - 1U) :
		        (u8)(index - 1U);
		GUI_Settings_SetToastDuration(&s_settings, s_toast_duration_presets[index]);
	}
	else if (key == LV_KEY_ESC)
	{
		(void)Page_Back();
	}
	else if (key != LV_KEY_ENTER)
	{
		(void)GUI_Settings_HandleKey(key);
	}
}

static void GUI_Settings_MessageTestEvent(lv_event_t *event)
{
	const GUI_SettingsMsgTestItem_t *item =
		(const GUI_SettingsMsgTestItem_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;

	if (item == NULL || s_settings.about_visible != 0U ||
	    s_settings.debug_visible != 0U)
	{
		return;
	}

	if (code == LV_EVENT_CLICKED ||
	    (code == LV_EVENT_KEY && (lv_event_get_key(event) == LV_KEY_ENTER ||
	                              lv_event_get_key(event) == LV_KEY_RIGHT)))
	{
		GUI_Settings_WaitInputRelease();
		if (GUI_Message_Show(item->kind, NULL, GUI_SETTINGS_MSG_TEST_TEXT,
		                     NULL, NULL) == SYSTEM_STATUS_OK)
		{
			GUI_Settings_SetStatus(&s_settings, "Message box shown");
		}
		else
		{
			GUI_Settings_SetStatus(&s_settings, "Message box unavailable");
		}
	}
	else if (code == LV_EVENT_KEY && (lv_event_get_key(event) == LV_KEY_ESC ||
	                                  lv_event_get_key(event) == LV_KEY_LEFT))
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

static void GUI_Settings_DebugCloseEvent(lv_event_t *event)
{
	GUI_SettingsContext_t *context = (GUI_SettingsContext_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);

	if (context == NULL)
	{
		return;
	}

	if (code == LV_EVENT_CLICKED || (code == LV_EVENT_KEY &&
	    (lv_event_get_key(event) == LV_KEY_ENTER ||
	     lv_event_get_key(event) == LV_KEY_ESC ||
	     lv_event_get_key(event) == LV_KEY_LEFT)))
	{
		GUI_Settings_HideDebug(context);
	}
	else if (code == LV_EVENT_KEY)
	{
		(void)GUI_Settings_HandleKey(lv_event_get_key(event));
	}
}

static void GUI_Settings_DebugItemEvent(lv_event_t *event)
{
	const GUI_SettingsDebugItem_t *item =
		(const GUI_SettingsDebugItem_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;

	if (item == NULL || s_settings.debug_visible == 0U)
	{
		return;
	}
	if (code == LV_EVENT_CLICKED)
	{
		GUI_Settings_ToggleDebugItem(&s_settings, item->id);
		return;
	}
	if (code != LV_EVENT_KEY)
	{
		return;
	}

	key = lv_event_get_key(event);
	if (GUI_Settings_IsSwitchToggleKey(key) != 0U)
	{
		/* Keypad ENTER also emits CLICKED when released; toggle there once. */
		return;
	}
	else if (key == LV_KEY_ESC || key == LV_KEY_LEFT)
	{
		GUI_Settings_HideDebug(&s_settings);
	}
	else
	{
		(void)GUI_Settings_HandleKey(key);
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

static u8 GUI_Settings_IsSwitchToggleKey(u32 key)
{
	return (key == GUI_SETTINGS_SWITCH_TOGGLE_KEY) ? 1U : 0U;
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
				if (GUI_Message_Show(GUI_MESSAGE_KIND_YESNO, "Install Font",
				                     "Pick an XBF font file and write it to SPI flash? The font area is erased.",
				                     GUI_Settings_FontInstallResult, context) != SYSTEM_STATUS_OK)
				{
					GUI_Settings_SetStatus(context, "Dialog unavailable");
				}
				else
				{
					GUI_Settings_SetStatus(context, "Confirm font install");
				}
			}
			break;
		case GUI_SETTINGS_ACTION_FONT_UNINSTALL:
			GUI_Settings_WaitInputRelease();
			if (GUI_Message_Show(GUI_MESSAGE_KIND_YESNO, "Uninstall Font",
			                     "Remove the installed font? Text pages fall back to the built-in font.",
			                     GUI_Settings_FontUninstallResult, context) != SYSTEM_STATUS_OK)
			{
				GUI_Settings_SetStatus(context, "Dialog unavailable");
			}
			else
			{
				GUI_Settings_SetStatus(context, "Confirm font uninstall");
			}
			break;
		case GUI_SETTINGS_ACTION_UPDATE:
			if (System_Settings_UsbMscEnabled() != 0U)
			{
				GUI_Settings_SetStatus(context, "Disable USB MSC before update");
			}
			else
			{
				GUI_Settings_WaitInputRelease();
				if (GUI_Message_Show(GUI_MESSAGE_KIND_YESNO, "Install Update",
				                     "Verify 0:/update.bin, stage it and reboot to flash? Do not power off.",
				                     GUI_Settings_UpdateResult, context) != SYSTEM_STATUS_OK)
				{
					GUI_Settings_SetStatus(context, "Dialog unavailable");
				}
				else
				{
					GUI_Settings_SetStatus(context, "Confirm firmware update");
				}
			}
			break;
		case GUI_SETTINGS_ACTION_DEBUG:
			GUI_Settings_ShowDebug(context);
			break;
		case GUI_SETTINGS_ACTION_ABOUT:
			GUI_Settings_ShowAbout(context);
			break;
		default:
			break;
	}
}

/* Yes/No callbacks run in the LVGL thread after the dialog closed and the
 * previous focus was restored, so page switches and focus rebuilds are safe. */
static void GUI_Settings_FontInstallResult(GUI_Message_Result_t result, void *user_data)
{
	GUI_SettingsContext_t *context = (GUI_SettingsContext_t *)user_data;

	if (context == NULL)
	{
		return;
	}
	if (result != GUI_MESSAGE_RESULT_YES)
	{
		GUI_Settings_SetStatus(context, "Install canceled");
		return;
	}

	if (GUI_Font_Show() == SYSTEM_STATUS_OK)
	{
		GUI_Settings_SetStatus(context, "Select XBF font bin");
	}
	else
	{
		GUI_Settings_SetStatus(context, "Font installer failed");
	}
}

static void GUI_Settings_FontUninstallResult(GUI_Message_Result_t result, void *user_data)
{
	GUI_SettingsContext_t *context = (GUI_SettingsContext_t *)user_data;

	if (context == NULL)
	{
		return;
	}
	if (result != GUI_MESSAGE_RESULT_YES)
	{
		GUI_Settings_SetStatus(context, "Uninstall canceled");
		return;
	}

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
}

static void GUI_Settings_UpdateResult(GUI_Message_Result_t result, void *user_data)
{
	GUI_SettingsContext_t *context = (GUI_SettingsContext_t *)user_data;
	IAP_Status_t status;

	if (context == NULL)
	{
		return;
	}
	if (result != GUI_MESSAGE_RESULT_YES)
	{
		GUI_Settings_SetStatus(context, "Update canceled");
		return;
	}

	status = IAP_PrepareUpdate(GUI_Settings_UpdateProgress, context);
	GUI_Settings_SetStatus(context, IAP_StatusString(status));
	lv_refr_now(NULL);
	if (status == IAP_STATUS_OK)
	{
		(void)osDelay(500U);
		NVIC_SystemReset();
	}
}

static void GUI_Settings_UpdateProgress(u8 percent, void *context_ptr)
{
	GUI_SettingsContext_t *context = (GUI_SettingsContext_t *)context_ptr;
	char text[32];

	if (context == NULL)
	{
		return;
	}

	(void)snprintf(text, sizeof(text), "Preparing update %u%%", (unsigned int)percent);
	GUI_Settings_SetStatus(context, text);
	lv_refr_now(NULL);
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

static void GUI_Settings_ShowDebug(GUI_SettingsContext_t *context)
{
	if (context == NULL || context->debug_panel == NULL)
	{
		return;
	}

	GUI_Settings_UpdateDebugSwitches(context);
	lv_obj_clear_flag(context->debug_panel, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(context->debug_panel);
	context->debug_visible = 1U;
	GUI_Settings_SetStatus(context, "Debug");
	GUI_Settings_RebuildFocus(context, context->debug_btns[0]);
	GUI_Settings_WaitInputRelease();
}

static void GUI_Settings_HideDebug(GUI_SettingsContext_t *context)
{
	if (context == NULL || context->debug_panel == NULL)
	{
		return;
	}

	lv_obj_add_flag(context->debug_panel, LV_OBJ_FLAG_HIDDEN);
	context->debug_visible = 0U;
	if (Page_GetCurrent() == GUI_PAGE_SETTINGS)
	{
		GUI_Settings_RebuildFocus(context, context->debug_btn);
		GUI_Settings_WaitInputRelease();
	}
}

static u8 GUI_Settings_DebugItemValue(GUI_SettingsDebugItemId_t id)
{
	switch (id)
	{
		case GUI_SETTINGS_DEBUG_MASTER:
			return System_Settings.Debug;
		case GUI_SETTINGS_DEBUG_SHELL:
			return System_Settings.DebugShellEnabled;
		case GUI_SETTINGS_DEBUG_AUDIO:
			return System_Settings.DebugAudioLogEnabled;
		case GUI_SETTINGS_DEBUG_VIDEO:
			return System_Settings.DebugVideoLogEnabled;
		case GUI_SETTINGS_DEBUG_LIBJPEG:
			return System_Settings.DebugLibjpegLogEnabled;
		case GUI_SETTINGS_DEBUG_FATFS:
			return System_Settings.DebugFatfsLogEnabled;
		case GUI_SETTINGS_DEBUG_MEMORY_DUMP:
			return System_Settings.MemoryDumpEnabled;
		case GUI_SETTINGS_DEBUG_DUMP_CCM:
			return System_Settings.MemoryDumpCcmEnabled;
		case GUI_SETTINGS_DEBUG_DUMP_MAIN:
			return System_Settings.MemoryDumpMainSramEnabled;
		case GUI_SETTINGS_DEBUG_DUMP_SRAMEX:
			return System_Settings.MemoryDumpSramexEnabled;
		default:
			return 0U;
	}
}

static void GUI_Settings_UpdateDebugSwitches(GUI_SettingsContext_t *context)
{
	u32 index;

	if (context == NULL)
	{
		return;
	}
	for (index = 0U; index < GUI_SETTINGS_DEBUG_ITEM_COUNT; index++)
	{
		if (context->debug_switches[index] == NULL)
		{
			continue;
		}
		if (GUI_Settings_DebugItemValue((GUI_SettingsDebugItemId_t)index) != 0U)
		{
			lv_obj_add_state(context->debug_switches[index], LV_STATE_CHECKED);
		}
		else
		{
			lv_obj_clear_state(context->debug_switches[index], LV_STATE_CHECKED);
		}
	}
}

static void GUI_Settings_ToggleDebugItem(GUI_SettingsContext_t *context,
	GUI_SettingsDebugItemId_t id)
{
	System_Settings_t previous;
	u8 enabled;

	if (context == NULL || (u32)id >= GUI_SETTINGS_DEBUG_ITEM_COUNT)
	{
		return;
	}
	previous = System_Settings;
	enabled = (GUI_Settings_DebugItemValue(id) == 0U) ? 1U : 0U;

	switch (id)
	{
		case GUI_SETTINGS_DEBUG_MASTER:
			System_Settings.Debug = enabled;
			break;
		case GUI_SETTINGS_DEBUG_SHELL:
			System_Settings.DebugShellEnabled = enabled;
			break;
		case GUI_SETTINGS_DEBUG_AUDIO:
			System_Settings.DebugAudioLogEnabled = enabled;
			break;
		case GUI_SETTINGS_DEBUG_VIDEO:
			System_Settings.DebugVideoLogEnabled = enabled;
			break;
		case GUI_SETTINGS_DEBUG_LIBJPEG:
			System_Settings.DebugLibjpegLogEnabled = enabled;
			break;
		case GUI_SETTINGS_DEBUG_FATFS:
			System_Settings.DebugFatfsLogEnabled = enabled;
			break;
		case GUI_SETTINGS_DEBUG_MEMORY_DUMP:
			System_Settings.MemoryDumpEnabled = enabled;
			break;
		case GUI_SETTINGS_DEBUG_DUMP_CCM:
			System_Settings.MemoryDumpCcmEnabled = enabled;
			break;
		case GUI_SETTINGS_DEBUG_DUMP_MAIN:
			System_Settings.MemoryDumpMainSramEnabled = enabled;
			break;
		case GUI_SETTINGS_DEBUG_DUMP_SRAMEX:
			System_Settings.MemoryDumpSramexEnabled = enabled;
			break;
		default:
			return;
	}

	if (System_Settings_Save() != SYSTEM_STATUS_OK)
	{
		System_Settings = previous;
		GUI_Settings_SetStatus(context, "Debug setting save failed");
	}
	else if (id == GUI_SETTINGS_DEBUG_MASTER || id == GUI_SETTINGS_DEBUG_SHELL)
	{
		GUI_Settings_SetStatus(context, "Restart to apply Debug/Shell");
	}
	else
	{
		GUI_Settings_SetStatus(context, "Debug setting saved");
	}
	GUI_Settings_UpdateDebugSwitches(context);
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
		               "Star Gamer 1.1.5-preview-oc\n"
		               "CPU Clock:%d\n"
		               "MCU/LCD: STM32F407ZGT6 / ST7789\n"
		               "Audio: PCM5102\n"
		               "Flash/EEPROM: W25Q64 / AT24C02\n"
		               "SRAM: IS62WV51216HBLL\n"
		               "CPUID: 0x%08lX\n"
		               "W25Q64 ID: 0x%02X%04X",
									 (unsigned int)SystemCoreClock,
		               (unsigned long)cpuid,
		               (unsigned int)mid,
		               (unsigned int)did);
	}
	else
	{
		(void)snprintf(context->about_text, sizeof(context->about_text),
		               "Star Gamer 1.1.5-preview-oc\n"
		               "CPU Clock:%d\n"
		               "MCU/LCD: STM32F407ZGT6 / ST7789\n"
		               "Audio: PCM5102\n"
		               "Flash/EEPROM: W25Q64 / AT24C02\n"
		               "SRAM: IS62WV51216HBLL\n"
		               "CPUID: 0x%08lX\n"
		               "W25Q64 ID: read failed",
									 (unsigned int)SystemCoreClock,
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
		if (context->usb_msc_btn == NULL)
		{
			context->first_item_btn = context->font_uninstall_btn;
		}
	}
	else
	{
		GUI_Settings_SetListButtonEnabled(context->font_install_btn, 1U);
		GUI_Settings_SetListButtonEnabled(context->font_uninstall_btn, 0U);
		if (context->usb_msc_btn == NULL)
		{
			context->first_item_btn = context->font_install_btn;
		}
	}
}

static void GUI_Settings_UpdateThemeSwitch(GUI_SettingsContext_t *context)
{
	if (context == NULL || context->theme_switch == NULL)
	{
		return;
	}

	if (System_Settings_ThemeLightEnabled() != 0U)
	{
		lv_obj_add_state(context->theme_switch, LV_STATE_CHECKED);
	}
	else
	{
		lv_obj_clear_state(context->theme_switch, LV_STATE_CHECKED);
	}
}

static void GUI_Settings_SetThemeLightEnabled(GUI_SettingsContext_t *context, u8 enabled)
{
	u8 old_enabled;

	if (context == NULL)
	{
		return;
	}

	old_enabled = System_Settings_ThemeLightEnabled();
	enabled = (enabled != 0U) ? 1U : 0U;
	if (old_enabled == enabled)
	{
		GUI_Settings_UpdateThemeSwitch(context);
		return;
	}

	if (System_Settings_SetThemeLightEnabled(enabled) != SYSTEM_STATUS_OK)
	{
		GUI_Settings_UpdateThemeSwitch(context);
		GUI_Settings_SetStatus(context, "Theme save failed");
		return;
	}

	if (GUI_Theme_Apply() != SYSTEM_STATUS_OK)
	{
		(void)System_Settings_SetThemeLightEnabled(old_enabled);
		(void)GUI_Theme_Apply();
		GUI_Settings_UpdateThemeSwitch(context);
		GUI_Settings_SetStatus(context, "Theme apply failed");
		return;
	}

	GUI_Settings_UpdateThemeSwitch(context);
	GUI_Settings_SetStatus(context, (enabled != 0U) ? "Light theme enabled" : "Dark theme enabled");
	GUI_Settings_WaitInputRelease();
	if (lv_async_call(GUI_Settings_ThemeReloadAsync, NULL) != LV_RES_OK)
	{
		GUI_Settings_SetStatus(context, "Reopen Settings to refresh theme");
	}
}

static void GUI_Settings_ThemeReloadAsync(void *user_data)
{
	(void)user_data;

	if (Page_GetCurrent() == GUI_PAGE_SETTINGS)
	{
		(void)Page_Delete(GUI_PAGE_SETTINGS);
		(void)GUI_Settings_Register();
		(void)GUI_Settings_Show();
	}
}

static void GUI_Settings_UpdateUsbMscSwitch(GUI_SettingsContext_t *context)
{
	if (context == NULL || context->usb_msc_switch == NULL)
	{
		return;
	}

	if (System_Settings_UsbMscEnabled() != 0U)
	{
		lv_obj_add_state(context->usb_msc_switch, LV_STATE_CHECKED);
	}
	else
	{
		lv_obj_clear_state(context->usb_msc_switch, LV_STATE_CHECKED);
	}
}

static void GUI_Settings_SetUsbMscEnabled(GUI_SettingsContext_t *context, u8 enabled)
{
	u8 old_enabled;

	if (context == NULL)
	{
		return;
	}

	old_enabled = System_Settings_UsbMscEnabled();
	enabled = (enabled != 0U) ? 1U : 0U;
	if (old_enabled == enabled)
	{
		GUI_Settings_UpdateUsbMscSwitch(context);
		return;
	}

	if (System_Settings_SetUsbMscEnabled(enabled) != SYSTEM_STATUS_OK)
	{
		System_Settings.UsbMscEnabled = old_enabled;
		GUI_Settings_UpdateUsbMscSwitch(context);
		GUI_Settings_SetStatus(context, "USB setting save failed");
		return;
	}

	if (USB_DEVICE_MSC_ApplySetting() != SYSTEM_STATUS_OK)
	{
		GUI_Settings_UpdateUsbMscSwitch(context);
		GUI_Settings_SetStatus(context, "USB MSC apply failed");
		return;
	}

	GUI_Settings_UpdateUsbMscSwitch(context);
	GUI_Settings_SetStatus(context, (enabled != 0U) ? "USB MSC enabled" : "USB MSC disabled");
}

static void GUI_Settings_UpdateClock(GUI_SettingsContext_t *context)
{
	u8 profile;

	if (context == NULL || context->clock_value == NULL)
	{
		return;
	}

	profile = System_Settings_ClockProfile();
	lv_label_set_text(context->clock_value, s_clock_labels[profile]);
}

static void GUI_Settings_SetClockProfile(GUI_SettingsContext_t *context, u8 profile)
{
	if (context == NULL || profile >= SYSTEM_CLOCK_PROFILE_COUNT)
	{
		return;
	}

	if (System_Settings_SetClockProfile(profile) != SYSTEM_STATUS_OK)
	{
		GUI_Settings_UpdateClock(context);
		GUI_Settings_SetStatus(context, "Clock setting save failed");
		return;
	}

	GUI_Settings_UpdateClock(context);
	GUI_Settings_SetStatus(context, "Clock saved; reset MCU to apply");
	GUI_Settings_WaitInputRelease();
}

static void GUI_Settings_UpdateAxfStack(GUI_SettingsContext_t *context)
{
	u8 level;

	if (context == NULL || context->axf_stack_value == NULL)
	{
		return;
	}
	level = System_Settings_AxfStackLevel();
	if (level >= SYSTEM_AXF_STACK_LEVEL_COUNT)
	{
		level = SYSTEM_AXF_STACK_16KB;
	}
	lv_label_set_text(context->axf_stack_value, s_axf_stack_labels[level]);
}

static void GUI_Settings_SetAxfStackLevel(GUI_SettingsContext_t *context, u8 level)
{
	if (context == NULL || level >= SYSTEM_AXF_STACK_LEVEL_COUNT)
	{
		return;
	}
	if (System_Settings_SetAxfStackLevel(level) != SYSTEM_STATUS_OK)
	{
		GUI_Settings_UpdateAxfStack(context);
		GUI_Settings_SetStatus(context, "AXF stack save failed");
		return;
	}

	GUI_Settings_UpdateAxfStack(context);
	GUI_Settings_SetStatus(context, s_axf_stack_labels[level]);
}

static void GUI_Settings_UpdateToastDuration(GUI_SettingsContext_t *context)
{
	u8 seconds;

	if (context == NULL || context->toast_duration_value == NULL)
	{
		return;
	}

	seconds = System_Settings_ToastDurationSec();
	(void)snprintf(s_toast_value_text, sizeof(s_toast_value_text), "%us",
	               (unsigned int)seconds);
	lv_label_set_text(context->toast_duration_value, s_toast_value_text);
}

static void GUI_Settings_SetToastDuration(GUI_SettingsContext_t *context, u8 seconds)
{
	if (context == NULL)
	{
		return;
	}

	if (System_Settings_SetToastDuration(seconds) != SYSTEM_STATUS_OK)
	{
		GUI_Settings_UpdateToastDuration(context);
		GUI_Settings_SetStatus(context, "Toast duration save failed");
		return;
	}

	GUI_Settings_UpdateToastDuration(context);
	GUI_Settings_SetStatus(context, "Toast duration saved");
}

static u8 GUI_Settings_ToastDurationIndex(u8 seconds)
{
	u8 i;

	for (i = 0U; i < GUI_SETTINGS_TOAST_DURATION_COUNT; i++)
	{
		if (s_toast_duration_presets[i] == seconds)
		{
			return i;
		}
	}

	return GUI_SETTINGS_TOAST_DURATION_DEFAULT_INDEX;
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
	else if (context->debug_visible != 0U)
	{
		GUI_Settings_AddFocusableChildren(group, context->debug_panel, &first_obj);
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

		if (GUI_Settings_IsInternalSwitch(child) != 0U)
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

static u8 GUI_Settings_IsInternalSwitch(lv_obj_t *object)
{
	u32 index;

	if (object == s_settings.usb_msc_switch)
	{
		return 1U;
	}
	if (object == s_settings.theme_switch)
	{
		return 1U;
	}
	for (index = 0U; index < GUI_SETTINGS_DEBUG_ITEM_COUNT; index++)
	{
		if (object == s_settings.debug_switches[index])
		{
			return 1U;
		}
	}
	return 0U;
}
