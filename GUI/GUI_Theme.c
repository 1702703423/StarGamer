#include "GUI_Theme.h"
#include "System_Status.h"

#define GUI_THEME_PRIMARY_DARK       0x26A69AU
#define GUI_THEME_SECONDARY_DARK     0xF9A825U
#define GUI_THEME_PRIMARY_LIGHT      0x00796BU
#define GUI_THEME_SECONDARY_LIGHT    0xF9A825U

static const u32 s_theme_dark[GUI_THEME_COLOR_MAX] =
{
	0x101418U, /* SCREEN_BG */
	0x141A20U, /* PANEL_BG */
	0x303844U, /* PANEL_BORDER */
	0x303844U, /* HEADER_LINE */
	0x252D36U, /* TOOL_BG */
	0x344150U, /* TOOL_BG_PRESSED */
	0x182028U, /* ROW_BG */
	0x263240U, /* ROW_BG_PRESSED */
	0x22303AU, /* ROW_BG_FOCUSED */
	0x151A20U, /* ROW_BG_DISABLED */
	0xF5F7FAU, /* TEXT_PRIMARY */
	0xEEF2F6U, /* TEXT_NORMAL */
	0xDCE3EAU, /* TEXT_SECONDARY */
	0x8E98A4U, /* TEXT_MUTED */
	0x6F7A86U, /* TEXT_DISABLED */
	0xFFFFFFU, /* TEXT_FOCUSED */
	0x303844U, /* SWITCH_BG */
	0xF5F7FAU, /* SWITCH_KNOB */
	0x2B3642U, /* SLIDER_BG */
	0x151D25U  /* VIEWER_BG */
};

static const u32 s_theme_light[GUI_THEME_COLOR_MAX] =
{
	0xF4F7FAU, /* SCREEN_BG */
	0xFFFFFFU, /* PANEL_BG */
	0xCBD5E1U, /* PANEL_BORDER */
	0xD6DEE8U, /* HEADER_LINE */
	0xE8EEF5U, /* TOOL_BG */
	0xD8E0EAU, /* TOOL_BG_PRESSED */
	0xF8FAFCU, /* ROW_BG */
	0xE5EBF2U, /* ROW_BG_PRESSED */
	0xFFF3C4U, /* ROW_BG_FOCUSED */
	0xEDF1F6U, /* ROW_BG_DISABLED */
	0x18202AU, /* TEXT_PRIMARY */
	0x243041U, /* TEXT_NORMAL */
	0x526070U, /* TEXT_SECONDARY */
	0x718092U, /* TEXT_MUTED */
	0xAAB3BEU, /* TEXT_DISABLED */
	0x111827U, /* TEXT_FOCUSED */
	0xCAD4DFU, /* SWITCH_BG */
	0xFFFFFFU, /* SWITCH_KNOB */
	0xD8E0EAU, /* SLIDER_BG */
	0xFFFFFFU  /* VIEWER_BG */
};

int GUI_Theme_Apply(void)
{
	lv_disp_t *display;
	lv_theme_t *theme;
	u8 light;

	display = lv_disp_get_default();
	if (display == NULL)
	{
		return OPERATE_FAILED;
	}

	light = GUI_Theme_IsLight();
	theme = lv_theme_default_init(display,
		lv_color_hex((light != 0U) ? GUI_THEME_PRIMARY_LIGHT : GUI_THEME_PRIMARY_DARK),
		lv_color_hex((light != 0U) ? GUI_THEME_SECONDARY_LIGHT : GUI_THEME_SECONDARY_DARK),
		light == 0U,
		LV_FONT_DEFAULT);
	if (theme == NULL)
	{
		return OPERATE_FAILED;
	}

	lv_disp_set_theme(display, theme);
	lv_disp_set_bg_color(display, GUI_Theme_LvColor(GUI_THEME_COLOR_SCREEN_BG));
	lv_obj_report_style_change(NULL);
	return SYSTEM_STATUS_OK;
}

u8 GUI_Theme_IsLight(void)
{
	return System_Settings_ThemeLightEnabled();
}

const char *GUI_Theme_Name(void)
{
	return (GUI_Theme_IsLight() != 0U) ? "Light" : "Dark";
}

u32 GUI_Theme_Color(GUI_ThemeColor_t color)
{
	const u32 *palette;

	if ((u32)color >= (u32)GUI_THEME_COLOR_MAX)
	{
		color = GUI_THEME_COLOR_SCREEN_BG;
	}

	palette = (GUI_Theme_IsLight() != 0U) ? s_theme_light : s_theme_dark;
	return palette[(u32)color];
}

lv_color_t GUI_Theme_LvColor(GUI_ThemeColor_t color)
{
	return lv_color_hex(GUI_Theme_Color(color));
}
