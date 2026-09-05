#include "GUI_Theme.h"
#include "System_Status.h"

/*
 * Material Design 3 baseline tonal scheme.
 * Values follow the MD3 baseline (Material You reference palette).
 */

/* --- key colors used to seed the LVGL default theme --- */
#define GUI_M3_SEED_PRIMARY_DARK        0xD0BCFFU
#define GUI_M3_SEED_SECONDARY_DARK      0xCCC2DCU
#define GUI_M3_SEED_PRIMARY_LIGHT       0x6750A4U
#define GUI_M3_SEED_SECONDARY_LIGHT     0x625B71U

static const u32 s_theme_dark[GUI_THEME_COLOR_MAX] =
{
	/* --- legacy aliases mapped onto MD3 roles --- */
	0x141218U, /* SCREEN_BG            <- surface                  */
	0x1D1B20U, /* PANEL_BG             <- surface-container-low    */
	0x49454FU, /* PANEL_BORDER         <- outline-variant          */
	0x49454FU, /* HEADER_LINE          <- outline-variant          */
	0x2B2930U, /* TOOL_BG              <- surface-container-high   */
	0x36343BU, /* TOOL_BG_PRESSED      <- surface-container-highest*/
	0x211F26U, /* ROW_BG               <- surface-container        */
	0x2B2930U, /* ROW_BG_PRESSED       <- surface-container-high   */
	0x4A4458U, /* ROW_BG_FOCUSED       <- secondary-container      */
	0x0F0D13U, /* ROW_BG_DISABLED      <- surface-container-lowest */
	0xE6E0E9U, /* TEXT_PRIMARY         <- on-surface               */
	0xE6E0E9U, /* TEXT_NORMAL          <- on-surface               */
	0xCAC4D0U, /* TEXT_SECONDARY       <- on-surface-variant       */
	0x938F99U, /* TEXT_MUTED           <- outline                  */
	0x646067U, /* TEXT_DISABLED        <- on-surface @ 38 %        */
	0xE8DEF8U, /* TEXT_FOCUSED         <- on-secondary-container   */
	0x36343BU, /* SWITCH_BG            <- surface-container-highest*/
	0x938F99U, /* SWITCH_KNOB          <- outline                  */
	0x36343BU, /* SLIDER_BG            <- surface-container-highest*/
	0x0F0D13U, /* VIEWER_BG            <- surface-container-lowest */

	/* --- MD3 color roles --- */
	0xD0BCFFU, /* PRIMARY                  */
	0x381E72U, /* ON_PRIMARY               */
	0x4F378BU, /* PRIMARY_CONTAINER        */
	0xEADDFFU, /* ON_PRIMARY_CONTAINER     */
	0xCCC2DCU, /* SECONDARY                */
	0x332D41U, /* ON_SECONDARY             */
	0x4A4458U, /* SECONDARY_CONTAINER      */
	0xE8DEF8U, /* ON_SECONDARY_CONTAINER   */
	0xEFB8C8U, /* TERTIARY                 */
	0x492532U, /* ON_TERTIARY              */
	0x633B48U, /* TERTIARY_CONTAINER       */
	0xFFD8E4U, /* ON_TERTIARY_CONTAINER    */
	0xF2B8B5U, /* ERROR                    */
	0x601410U, /* ON_ERROR                 */
	0x8C1D18U, /* ERROR_CONTAINER          */
	0xF9DEDCU, /* ON_ERROR_CONTAINER       */
	0x141218U, /* SURFACE                  */
	0xE6E0E9U, /* ON_SURFACE               */
	0x49454FU, /* SURFACE_VARIANT          */
	0xCAC4D0U, /* ON_SURFACE_VARIANT       */
	0x141218U, /* SURFACE_DIM              */
	0x3B383EU, /* SURFACE_BRIGHT           */
	0x0F0D13U, /* SURFACE_CONTAINER_LOWEST */
	0x1D1B20U, /* SURFACE_CONTAINER_LOW    */
	0x211F26U, /* SURFACE_CONTAINER        */
	0x2B2930U, /* SURFACE_CONTAINER_HIGH   */
	0x36343BU, /* SURFACE_CONTAINER_HIGHEST*/
	0x938F99U, /* OUTLINE                  */
	0x49454FU, /* OUTLINE_VARIANT          */
	0xE6E0E9U, /* INVERSE_SURFACE          */
	0x322F35U, /* INVERSE_ON_SURFACE       */
	0x6750A4U, /* INVERSE_PRIMARY          */
	0x000000U  /* SCRIM                    */
};

static const u32 s_theme_light[GUI_THEME_COLOR_MAX] =
{
	/* --- legacy aliases mapped onto MD3 roles --- */
	0xFEF7FFU, /* SCREEN_BG            <- surface                  */
	0xF7F2FAU, /* PANEL_BG             <- surface-container-low    */
	0xCAC4D0U, /* PANEL_BORDER         <- outline-variant          */
	0xCAC4D0U, /* HEADER_LINE          <- outline-variant          */
	0xECE6F0U, /* TOOL_BG              <- surface-container-high   */
	0xE6E0E9U, /* TOOL_BG_PRESSED      <- surface-container-highest*/
	0xF3EDF7U, /* ROW_BG               <- surface-container        */
	0xECE6F0U, /* ROW_BG_PRESSED       <- surface-container-high   */
	0xE8DEF8U, /* ROW_BG_FOCUSED       <- secondary-container      */
	0xE8E2ECU, /* ROW_BG_DISABLED      <- surface-dim              */
	0x1D1B20U, /* TEXT_PRIMARY         <- on-surface               */
	0x1D1B20U, /* TEXT_NORMAL          <- on-surface               */
	0x49454FU, /* TEXT_SECONDARY       <- on-surface-variant       */
	0x79747EU, /* TEXT_MUTED           <- outline                  */
	0xA8A3AAU, /* TEXT_DISABLED        <- on-surface @ 38 %        */
	0x1D192BU, /* TEXT_FOCUSED         <- on-secondary-container   */
	0xE6E0E9U, /* SWITCH_BG            <- surface-container-highest*/
	0x79747EU, /* SWITCH_KNOB          <- outline                  */
	0xE6E0E9U, /* SLIDER_BG            <- surface-container-highest*/
	0xFFFFFFU, /* VIEWER_BG            <- surface-container-lowest */

	/* --- MD3 color roles --- */
	0x6750A4U, /* PRIMARY                  */
	0xFFFFFFU, /* ON_PRIMARY               */
	0xEADDFFU, /* PRIMARY_CONTAINER        */
	0x21005DU, /* ON_PRIMARY_CONTAINER     */
	0x625B71U, /* SECONDARY                */
	0xFFFFFFU, /* ON_SECONDARY             */
	0xE8DEF8U, /* SECONDARY_CONTAINER      */
	0x1D192BU, /* ON_SECONDARY_CONTAINER   */
	0x7D5260U, /* TERTIARY                 */
	0xFFFFFFU, /* ON_TERTIARY              */
	0xFFD8E4U, /* TERTIARY_CONTAINER       */
	0x31111DU, /* ON_TERTIARY_CONTAINER    */
	0xB3261EU, /* ERROR                    */
	0xFFFFFFU, /* ON_ERROR                 */
	0xF9DEDCU, /* ERROR_CONTAINER          */
	0x410E0BU, /* ON_ERROR_CONTAINER       */
	0xFEF7FFU, /* SURFACE                  */
	0x1D1B20U, /* ON_SURFACE               */
	0xE7E0ECU, /* SURFACE_VARIANT          */
	0x49454FU, /* ON_SURFACE_VARIANT       */
	0xDED8E1U, /* SURFACE_DIM              */
	0xFEF7FFU, /* SURFACE_BRIGHT           */
	0xFFFFFFU, /* SURFACE_CONTAINER_LOWEST */
	0xF7F2FAU, /* SURFACE_CONTAINER_LOW    */
	0xF3EDF7U, /* SURFACE_CONTAINER        */
	0xECE6F0U, /* SURFACE_CONTAINER_HIGH   */
	0xE6E0E9U, /* SURFACE_CONTAINER_HIGHEST*/
	0x79747EU, /* OUTLINE                  */
	0xCAC4D0U, /* OUTLINE_VARIANT          */
	0x322F35U, /* INVERSE_SURFACE          */
	0xF5EFF7U, /* INVERSE_ON_SURFACE       */
	0xD0BCFFU, /* INVERSE_PRIMARY          */
	0x000000U  /* SCRIM                    */
};

/* Compile time guard: both palettes must cover every color role. */
typedef char GUI_M3_DarkPaletteSizeCheck[(sizeof(s_theme_dark) / sizeof(s_theme_dark[0]) ==
                                          (u32)GUI_THEME_COLOR_MAX) ? 1 : -1];
typedef char GUI_M3_LightPaletteSizeCheck[(sizeof(s_theme_light) / sizeof(s_theme_light[0]) ==
                                           (u32)GUI_THEME_COLOR_MAX) ? 1 : -1];

/* ------------------------------------------------------------------------- */
/* Motion - one shared, short state-layer transition                          */
/* ------------------------------------------------------------------------- */

static const lv_style_prop_t s_transition_props[] =
{
	LV_STYLE_BG_COLOR,
	LV_STYLE_TEXT_COLOR,
	LV_STYLE_BORDER_COLOR,
	LV_STYLE_OUTLINE_WIDTH,
	LV_STYLE_OUTLINE_OPA,
	(lv_style_prop_t)0
};

static lv_style_transition_dsc_t s_transition;
static u8 s_transition_ready = 0U;

static const lv_style_transition_dsc_t *GUI_M3_Transition(void)
{
	if (s_transition_ready == 0U)
	{
		lv_style_transition_dsc_init(&s_transition, s_transition_props,
		                             lv_anim_path_ease_out, GUI_M3_MOTION_SHORT, 0U, NULL);
		s_transition_ready = 1U;
	}

	return &s_transition;
}

static void GUI_M3_ApplyTransition(lv_obj_t *obj)
{
	const lv_style_transition_dsc_t *dsc;

	if (obj == NULL)
	{
		return;
	}

	dsc = GUI_M3_Transition();
	lv_obj_set_style_transition(obj, dsc, LV_STATE_DEFAULT);
	lv_obj_set_style_transition(obj, dsc, LV_STATE_PRESSED);
	lv_obj_set_style_transition(obj, dsc, LV_STATE_FOCUSED);
}

/* ------------------------------------------------------------------------- */
/* Theme core                                                                 */
/* ------------------------------------------------------------------------- */

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
		lv_color_hex((light != 0U) ? GUI_M3_SEED_PRIMARY_LIGHT : GUI_M3_SEED_PRIMARY_DARK),
		lv_color_hex((light != 0U) ? GUI_M3_SEED_SECONDARY_LIGHT : GUI_M3_SEED_SECONDARY_DARK),
		light == 0U,
		LV_FONT_DEFAULT);
	if (theme == NULL)
	{
		return OPERATE_FAILED;
	}

	lv_disp_set_theme(display, theme);
	lv_disp_set_bg_color(display, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE));
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
		color = GUI_THEME_COLOR_SURFACE;
	}

	palette = (GUI_Theme_IsLight() != 0U) ? s_theme_light : s_theme_dark;
	return palette[(u32)color];
}

lv_color_t GUI_Theme_LvColor(GUI_ThemeColor_t color)
{
	return lv_color_hex(GUI_Theme_Color(color));
}

/* ------------------------------------------------------------------------- */
/* Design token helpers                                                       */
/* ------------------------------------------------------------------------- */

const lv_font_t *GUI_M3_Font(GUI_M3_TypeRole_t role)
{
	switch (role)
	{
		case GUI_M3_TYPE_HEADLINE:
#if LV_FONT_MONTSERRAT_20
			return &lv_font_montserrat_20;
#else
			return LV_FONT_DEFAULT;
#endif
		case GUI_M3_TYPE_TITLE:
#if LV_FONT_MONTSERRAT_16
			return &lv_font_montserrat_16;
#else
			return LV_FONT_DEFAULT;
#endif
		case GUI_M3_TYPE_LABEL:
		case GUI_M3_TYPE_BODY:
		default:
			return LV_FONT_DEFAULT;
	}
}

const lv_font_t *GUI_M3_IconFont(void)
{
	return &lv_font_md_icon_20;
}

const lv_font_t *GUI_M3_IconFontLarge(void)
{
	return &lv_font_md_icon_28;
}

lv_color_t GUI_M3_StateLayer(GUI_ThemeColor_t base, GUI_ThemeColor_t overlay, lv_opa_t opa)
{
	return lv_color_mix(GUI_Theme_LvColor(overlay), GUI_Theme_LvColor(base), opa);
}

/* ------------------------------------------------------------------------- */
/* Component API                                                              */
/* ------------------------------------------------------------------------- */

void GUI_M3_ApplyScreen(lv_obj_t *screen)
{
	if (screen == NULL)
	{
		return;
	}

	lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_bg_color(screen, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE), 0);
	lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);
	lv_obj_set_style_border_width(screen, 0, 0);
	lv_obj_set_style_outline_width(screen, 0, 0);
	lv_obj_set_style_radius(screen, 0, 0);
	lv_obj_set_style_pad_all(screen, 0, 0);
}

void GUI_M3_ApplyFocusRing(lv_obj_t *obj, GUI_ThemeColor_t color)
{
	if (obj == NULL)
	{
		return;
	}

	lv_obj_set_style_outline_width(obj, 0, LV_STATE_DEFAULT);
	lv_obj_set_style_outline_opa(obj, LV_OPA_TRANSP, LV_STATE_DEFAULT);
	lv_obj_set_style_outline_width(obj, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(obj, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_opa(obj, LV_OPA_COVER, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(obj, GUI_Theme_LvColor(color), LV_STATE_FOCUSED);
}

static void GUI_M3_SetContainerColors(lv_obj_t *obj, GUI_ThemeColor_t bg, GUI_ThemeColor_t fg)
{
	lv_obj_set_style_bg_color(obj, GUI_Theme_LvColor(bg), 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(obj, GUI_M3_StateLayer(bg, fg, GUI_M3_STATE_PRESSED_OPA), LV_STATE_PRESSED);
	lv_obj_set_style_bg_color(obj, GUI_M3_StateLayer(bg, fg, GUI_M3_STATE_FOCUS_OPA), LV_STATE_FOCUSED);
	lv_obj_set_style_text_color(obj, GUI_Theme_LvColor(fg), 0);
}

static void GUI_M3_SetTransparentColors(lv_obj_t *obj, GUI_ThemeColor_t fg)
{
	lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, 0);
	lv_obj_set_style_bg_color(obj, GUI_Theme_LvColor(fg), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(obj, GUI_M3_STATE_PRESSED_OPA, LV_STATE_PRESSED);
	lv_obj_set_style_bg_color(obj, GUI_Theme_LvColor(fg), LV_STATE_FOCUSED);
	lv_obj_set_style_bg_opa(obj, GUI_M3_STATE_FOCUS_OPA, LV_STATE_FOCUSED);
	lv_obj_set_style_text_color(obj, GUI_Theme_LvColor(fg), 0);
}

void GUI_M3_StyleButton(lv_obj_t *btn, GUI_M3_ButtonStyle_t style)
{
	if (btn == NULL)
	{
		return;
	}

	lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(btn, GUI_M3_RADIUS_FULL, 0);
	lv_obj_set_style_border_width(btn, 0, 0);
	lv_obj_set_style_shadow_width(btn, 0, 0);
	lv_obj_set_style_pad_all(btn, 0, 0);
	lv_obj_set_style_text_opa(btn, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, LV_STATE_DISABLED);
	lv_obj_set_style_bg_color(btn, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE_CONTAINER_HIGHEST), LV_STATE_DISABLED);
	lv_obj_set_style_text_color(btn, GUI_Theme_LvColor(GUI_THEME_COLOR_TEXT_DISABLED), LV_STATE_DISABLED);

	switch (style)
	{
		case GUI_M3_BUTTON_TONAL:
		case GUI_M3_BUTTON_ICON_TONAL:
			GUI_M3_SetContainerColors(btn, GUI_THEME_COLOR_SECONDARY_CONTAINER,
			                          GUI_THEME_COLOR_ON_SECONDARY_CONTAINER);
			break;

		case GUI_M3_BUTTON_OUTLINED:
			GUI_M3_SetTransparentColors(btn, GUI_THEME_COLOR_PRIMARY);
			lv_obj_set_style_border_width(btn, 1, 0);
			lv_obj_set_style_border_color(btn, GUI_Theme_LvColor(GUI_THEME_COLOR_OUTLINE), 0);
			lv_obj_set_style_border_color(btn, GUI_Theme_LvColor(GUI_THEME_COLOR_PRIMARY), LV_STATE_FOCUSED);
			lv_obj_set_style_border_opa(btn, LV_OPA_COVER, 0);
			break;

		case GUI_M3_BUTTON_TEXT:
			GUI_M3_SetTransparentColors(btn, GUI_THEME_COLOR_PRIMARY);
			break;

		case GUI_M3_BUTTON_ELEVATED:
			GUI_M3_SetContainerColors(btn, GUI_THEME_COLOR_SURFACE_CONTAINER_LOW, GUI_THEME_COLOR_PRIMARY);
			break;

		case GUI_M3_BUTTON_ICON:
			GUI_M3_SetTransparentColors(btn, GUI_THEME_COLOR_ON_SURFACE_VARIANT);
			break;

		case GUI_M3_BUTTON_ICON_FILLED:
			GUI_M3_SetContainerColors(btn, GUI_THEME_COLOR_PRIMARY, GUI_THEME_COLOR_ON_PRIMARY);
			break;

		case GUI_M3_BUTTON_FAB:
			GUI_M3_SetContainerColors(btn, GUI_THEME_COLOR_PRIMARY_CONTAINER,
			                          GUI_THEME_COLOR_ON_PRIMARY_CONTAINER);
			lv_obj_set_style_radius(btn, GUI_M3_RADIUS_LG, 0);
			break;

		case GUI_M3_BUTTON_ERROR:
			GUI_M3_SetContainerColors(btn, GUI_THEME_COLOR_ERROR, GUI_THEME_COLOR_ON_ERROR);
			break;

		case GUI_M3_BUTTON_FILLED:
		default:
			GUI_M3_SetContainerColors(btn, GUI_THEME_COLOR_PRIMARY, GUI_THEME_COLOR_ON_PRIMARY);
			break;
	}

	GUI_M3_ApplyFocusRing(btn, GUI_THEME_COLOR_PRIMARY);
	GUI_M3_ApplyTransition(btn);
}

lv_obj_t *GUI_M3_CreateIconButton(lv_obj_t *parent, const char *icon, GUI_M3_ButtonStyle_t style)
{
	lv_obj_t *btn;
	lv_obj_t *label;

	if (parent == NULL)
	{
		return NULL;
	}

	btn = lv_btn_create(parent);
	if (btn == NULL)
	{
		return NULL;
	}

	lv_obj_set_size(btn, GUI_M3_ICON_BTN_SIZE, GUI_M3_ICON_BTN_SIZE);
	GUI_M3_StyleButton(btn, style);

	label = lv_label_create(btn);
	if (label != NULL)
	{
		lv_label_set_text(label, (icon != NULL) ? icon : GUI_ICON_MORE_HORIZ);
		lv_obj_set_style_text_font(label, GUI_M3_IconFont(), 0);
		lv_obj_center(label);
	}

	return btn;
}

lv_obj_t *GUI_M3_CreateButton(lv_obj_t *parent, GUI_M3_ButtonStyle_t style, const char *icon, const char *text)
{
	lv_obj_t *btn;
	lv_obj_t *label;
	lv_obj_t *icon_label;

	if (parent == NULL)
	{
		return NULL;
	}

	btn = lv_btn_create(parent);
	if (btn == NULL)
	{
		return NULL;
	}

	GUI_M3_StyleButton(btn, style);
	lv_obj_set_style_pad_hor(btn, 14, 0);
	lv_obj_set_style_pad_ver(btn, 6, 0);
	lv_obj_set_style_pad_column(btn, 6, 0);
	lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

	if (icon != NULL)
	{
		icon_label = lv_label_create(btn);
		if (icon_label != NULL)
		{
			lv_label_set_text(icon_label, icon);
			lv_obj_set_style_text_font(icon_label, GUI_M3_IconFont(), 0);
		}
	}

	if (text != NULL)
	{
		label = lv_label_create(btn);
		if (label != NULL)
		{
			lv_label_set_text(label, text);
			lv_obj_set_style_text_font(label, GUI_M3_Font(GUI_M3_TYPE_LABEL), 0);
		}
	}

	return btn;
}

/* --- top app bar ------------------------------------------------------- */

#define GUI_M3_APPBAR_TITLE_INDEX   0U

static lv_obj_t *GUI_M3_AppBarActionArea(lv_obj_t *bar, u8 create)
{
	u32 i;
	u32 count;
	lv_obj_t *child;
	lv_obj_t *area;

	if (bar == NULL)
	{
		return NULL;
	}

	count = lv_obj_get_child_cnt(bar);
	for (i = 0U; i < count; i++)
	{
		child = lv_obj_get_child(bar, (s32)i);
		if (child != NULL && lv_obj_has_flag(child, LV_OBJ_FLAG_USER_1))
		{
			return child;
		}
	}

	if (create == 0U)
	{
		return NULL;
	}

	area = lv_obj_create(bar);
	if (area == NULL)
	{
		return NULL;
	}

	lv_obj_add_flag(area, LV_OBJ_FLAG_USER_1);
	lv_obj_clear_flag(area, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(area, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(area, LV_SIZE_CONTENT, GUI_M3_ICON_BTN_SIZE);
	lv_obj_set_style_bg_opa(area, LV_OPA_TRANSP, 0);
	lv_obj_set_style_border_width(area, 0, 0);
	lv_obj_set_style_radius(area, 0, 0);
	lv_obj_set_style_pad_all(area, 0, 0);
	lv_obj_set_style_pad_column(area, GUI_M3_ICON_BTN_GAP, 0);
	lv_obj_set_flex_flow(area, LV_FLEX_FLOW_ROW);
	lv_obj_set_flex_align(area, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
	lv_obj_align(area, LV_ALIGN_RIGHT_MID, -6, 0);

	return area;
}

lv_obj_t *GUI_M3_CreateTopAppBar(lv_obj_t *screen, const char *title)
{
	lv_obj_t *bar;
	lv_obj_t *label;

	if (screen == NULL)
	{
		return NULL;
	}

	bar = lv_obj_create(screen);
	if (bar == NULL)
	{
		return NULL;
	}

	lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(bar, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(bar, GUI_M3_SCREEN_W, GUI_M3_APPBAR_H);
	lv_obj_set_pos(bar, 0, 0);
	lv_obj_set_style_radius(bar, 0, 0);
	lv_obj_set_style_border_width(bar, 0, 0);
	lv_obj_set_style_shadow_width(bar, 0, 0);
	lv_obj_set_style_pad_all(bar, 0, 0);
	lv_obj_set_style_bg_color(bar, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE_CONTAINER), 0);
	lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);

	label = lv_label_create(bar);
	if (label != NULL)
	{
		lv_label_set_text(label, (title != NULL) ? title : "");
		lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
		lv_obj_set_width(label, 180);
		lv_obj_set_style_text_font(label, GUI_M3_Font(GUI_M3_TYPE_TITLE), 0);
		lv_obj_set_style_text_color(label, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE), 0);
		lv_obj_align(label, LV_ALIGN_LEFT_MID, 14, 0);
	}

	return bar;
}

lv_obj_t *GUI_M3_AppBarGetTitle(lv_obj_t *bar)
{
	if (bar == NULL || lv_obj_get_child_cnt(bar) == 0U)
	{
		return NULL;
	}

	return lv_obj_get_child(bar, (s32)GUI_M3_APPBAR_TITLE_INDEX);
}

lv_obj_t *GUI_M3_AppBarSetNav(lv_obj_t *bar, const char *icon, lv_event_cb_t cb, void *user_data)
{
	lv_obj_t *btn;
	lv_obj_t *title;

	if (bar == NULL)
	{
		return NULL;
	}

	btn = GUI_M3_CreateIconButton(bar, (icon != NULL) ? icon : GUI_ICON_ARROW_BACK, GUI_M3_BUTTON_ICON);
	if (btn == NULL)
	{
		return NULL;
	}

	lv_obj_align(btn, LV_ALIGN_LEFT_MID, 4, 0);
	if (cb != NULL)
	{
		lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
		lv_obj_add_event_cb(btn, cb, LV_EVENT_KEY, user_data);
	}

	title = GUI_M3_AppBarGetTitle(bar);
	if (title != NULL)
	{
		lv_obj_align(title, LV_ALIGN_LEFT_MID, 42, 0);
	}

	return btn;
}

lv_obj_t *GUI_M3_AppBarAddAction(lv_obj_t *bar, const char *icon, lv_event_cb_t cb, void *user_data)
{
	lv_obj_t *area;
	lv_obj_t *btn;

	area = GUI_M3_AppBarActionArea(bar, 1U);
	if (area == NULL)
	{
		return NULL;
	}

	btn = GUI_M3_CreateIconButton(area, icon, GUI_M3_BUTTON_ICON);
	if (btn == NULL)
	{
		return NULL;
	}

	if (cb != NULL)
	{
		lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user_data);
		lv_obj_add_event_cb(btn, cb, LV_EVENT_KEY, user_data);
	}

	return btn;
}

/* --- cards, labels, dividers ------------------------------------------- */

void GUI_M3_StyleCard(lv_obj_t *obj, GUI_M3_CardStyle_t style)
{
	if (obj == NULL)
	{
		return;
	}

	lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_style_radius(obj, GUI_M3_RADIUS_MD, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_shadow_width(obj, 0, 0);
	lv_obj_set_style_pad_all(obj, GUI_M3_PAD_CARD, 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_text_color(obj, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE), 0);

	switch (style)
	{
		case GUI_M3_CARD_ELEVATED:
			lv_obj_set_style_bg_color(obj, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE_CONTAINER_LOW), 0);
			break;

		case GUI_M3_CARD_OUTLINED:
			lv_obj_set_style_bg_color(obj, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE), 0);
			lv_obj_set_style_border_width(obj, 1, 0);
			lv_obj_set_style_border_color(obj, GUI_Theme_LvColor(GUI_THEME_COLOR_OUTLINE_VARIANT), 0);
			lv_obj_set_style_border_opa(obj, LV_OPA_COVER, 0);
			break;

		case GUI_M3_CARD_FILLED:
		default:
			lv_obj_set_style_bg_color(obj, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE_CONTAINER_HIGHEST), 0);
			break;
	}
}

lv_obj_t *GUI_M3_CreateCard(lv_obj_t *parent, GUI_M3_CardStyle_t style)
{
	lv_obj_t *card;

	if (parent == NULL)
	{
		return NULL;
	}

	card = lv_obj_create(parent);
	if (card == NULL)
	{
		return NULL;
	}

	GUI_M3_StyleCard(card, style);
	return card;
}

lv_obj_t *GUI_M3_CreateIconLabel(lv_obj_t *parent, const char *icon, GUI_ThemeColor_t color, u8 large)
{
	lv_obj_t *label;

	if (parent == NULL)
	{
		return NULL;
	}

	label = lv_label_create(parent);
	if (label == NULL)
	{
		return NULL;
	}

	lv_label_set_text(label, (icon != NULL) ? icon : GUI_ICON_MORE_HORIZ);
	lv_obj_set_style_text_font(label, (large != 0U) ? GUI_M3_IconFontLarge() : GUI_M3_IconFont(), 0);
	lv_obj_set_style_text_color(label, GUI_Theme_LvColor(color), 0);

	return label;
}

lv_obj_t *GUI_M3_CreateIconContainer(lv_obj_t *parent, const char *icon, lv_coord_t size,
                                     GUI_ThemeColor_t bg, GUI_ThemeColor_t fg, u8 large_icon)
{
	lv_obj_t *container;
	lv_obj_t *label;

	if (parent == NULL)
	{
		return NULL;
	}

	container = lv_obj_create(parent);
	if (container == NULL)
	{
		return NULL;
	}

	lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(container, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(container, size, size);
	lv_obj_set_style_radius(container, GUI_M3_RADIUS_FULL, 0);
	lv_obj_set_style_border_width(container, 0, 0);
	lv_obj_set_style_shadow_width(container, 0, 0);
	lv_obj_set_style_pad_all(container, 0, 0);
	lv_obj_set_style_bg_color(container, GUI_Theme_LvColor(bg), 0);
	lv_obj_set_style_bg_opa(container, LV_OPA_COVER, 0);

	label = GUI_M3_CreateIconLabel(container, icon, fg, large_icon);
	if (label != NULL)
	{
		lv_obj_center(label);
	}

	return container;
}

lv_obj_t *GUI_M3_CreateLabel(lv_obj_t *parent, const char *text, GUI_M3_TypeRole_t role, GUI_ThemeColor_t color)
{
	lv_obj_t *label;

	if (parent == NULL)
	{
		return NULL;
	}

	label = lv_label_create(parent);
	if (label == NULL)
	{
		return NULL;
	}

	lv_label_set_text(label, (text != NULL) ? text : "");
	lv_obj_set_style_text_font(label, GUI_M3_Font(role), 0);
	lv_obj_set_style_text_color(label, GUI_Theme_LvColor(color), 0);

	return label;
}

lv_obj_t *GUI_M3_CreateDivider(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w)
{
	lv_obj_t *line;

	if (parent == NULL)
	{
		return NULL;
	}

	line = lv_obj_create(parent);
	if (line == NULL)
	{
		return NULL;
	}

	lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(line, w, 1);
	lv_obj_set_pos(line, x, y);
	lv_obj_set_style_radius(line, 0, 0);
	lv_obj_set_style_border_width(line, 0, 0);
	lv_obj_set_style_pad_all(line, 0, 0);
	lv_obj_set_style_bg_color(line, GUI_Theme_LvColor(GUI_THEME_COLOR_OUTLINE_VARIANT), 0);
	lv_obj_set_style_bg_opa(line, LV_OPA_COVER, 0);

	return line;
}

/* --- lists, switches, sliders, indicators ------------------------------- */

void GUI_M3_StyleList(lv_obj_t *list)
{
	if (list == NULL)
	{
		return;
	}

	lv_obj_set_style_radius(list, GUI_M3_RADIUS_LG, 0);
	lv_obj_set_style_border_width(list, 0, 0);
	lv_obj_set_style_shadow_width(list, 0, 0);
	lv_obj_set_style_bg_color(list, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE_CONTAINER_LOW), 0);
	lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(list, 4, 0);
	lv_obj_set_style_pad_row(list, 2, 0);
	lv_obj_set_style_bg_color(list, GUI_Theme_LvColor(GUI_THEME_COLOR_OUTLINE), LV_PART_SCROLLBAR);
	lv_obj_set_style_bg_opa(list, LV_OPA_40, LV_PART_SCROLLBAR);
	lv_obj_set_style_radius(list, GUI_M3_RADIUS_FULL, LV_PART_SCROLLBAR);
	lv_obj_set_style_width(list, 4, LV_PART_SCROLLBAR);
}

void GUI_M3_StyleListItem(lv_obj_t *item)
{
	if (item == NULL)
	{
		return;
	}

	lv_obj_set_style_radius(item, GUI_M3_RADIUS_FULL, 0);
	lv_obj_set_style_border_width(item, 0, 0);
	lv_obj_set_style_shadow_width(item, 0, 0);
	lv_obj_set_style_outline_width(item, 0, 0);
	lv_obj_set_style_pad_hor(item, 12, 0);
	lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, 0);
	lv_obj_set_style_text_color(item, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE), 0);

	lv_obj_set_style_bg_color(item, GUI_M3_StateLayer(GUI_THEME_COLOR_SURFACE_CONTAINER_LOW,
	                                                  GUI_THEME_COLOR_ON_SURFACE,
	                                                  GUI_M3_STATE_PRESSED_OPA), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_PRESSED);

	lv_obj_set_style_bg_color(item, GUI_Theme_LvColor(GUI_THEME_COLOR_SECONDARY_CONTAINER), LV_STATE_FOCUSED);
	lv_obj_set_style_bg_opa(item, LV_OPA_COVER, LV_STATE_FOCUSED);
	lv_obj_set_style_text_color(item, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SECONDARY_CONTAINER), LV_STATE_FOCUSED);

	lv_obj_set_style_text_color(item, GUI_Theme_LvColor(GUI_THEME_COLOR_TEXT_DISABLED), LV_STATE_DISABLED);
	lv_obj_set_style_bg_opa(item, LV_OPA_TRANSP, LV_STATE_DISABLED);

	GUI_M3_ApplyTransition(item);
}

void GUI_M3_StyleSwitch(lv_obj_t *sw)
{
	if (sw == NULL)
	{
		return;
	}

	lv_obj_set_style_radius(sw, GUI_M3_RADIUS_FULL, LV_PART_MAIN);
	lv_obj_set_style_bg_color(sw, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE_CONTAINER_HIGHEST), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_width(sw, 2, LV_PART_MAIN);
	lv_obj_set_style_border_color(sw, GUI_Theme_LvColor(GUI_THEME_COLOR_OUTLINE), LV_PART_MAIN);
	lv_obj_set_style_border_opa(sw, LV_OPA_COVER, LV_PART_MAIN);

	lv_obj_set_style_bg_color(sw, GUI_Theme_LvColor(GUI_THEME_COLOR_PRIMARY), LV_PART_INDICATOR | LV_STATE_CHECKED);
	lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
	lv_obj_set_style_border_width(sw, 0, LV_PART_INDICATOR | LV_STATE_CHECKED);

	lv_obj_set_style_radius(sw, GUI_M3_RADIUS_FULL, LV_PART_KNOB);
	lv_obj_set_style_bg_color(sw, GUI_Theme_LvColor(GUI_THEME_COLOR_OUTLINE), LV_PART_KNOB);
	lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_KNOB);
	lv_obj_set_style_pad_all(sw, -6, LV_PART_KNOB);
	lv_obj_set_style_bg_color(sw, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_PRIMARY), LV_PART_KNOB | LV_STATE_CHECKED);
	lv_obj_set_style_pad_all(sw, -3, LV_PART_KNOB | LV_STATE_CHECKED);

	GUI_M3_ApplyFocusRing(sw, GUI_THEME_COLOR_PRIMARY);
}

void GUI_M3_StyleSlider(lv_obj_t *slider)
{
	if (slider == NULL)
	{
		return;
	}

	lv_obj_set_style_radius(slider, GUI_M3_RADIUS_FULL, LV_PART_MAIN);
	lv_obj_set_style_bg_color(slider, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE_CONTAINER_HIGHEST), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);

	lv_obj_set_style_radius(slider, GUI_M3_RADIUS_FULL, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(slider, GUI_Theme_LvColor(GUI_THEME_COLOR_PRIMARY), LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);

	lv_obj_set_style_radius(slider, GUI_M3_RADIUS_FULL, LV_PART_KNOB);
	lv_obj_set_style_bg_color(slider, GUI_Theme_LvColor(GUI_THEME_COLOR_PRIMARY), LV_PART_KNOB);
	lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
	lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB);
	lv_obj_set_style_pad_ver(slider, 6, LV_PART_KNOB);
	lv_obj_set_style_pad_hor(slider, -1, LV_PART_KNOB);

	GUI_M3_ApplyFocusRing(slider, GUI_THEME_COLOR_PRIMARY);
}

void GUI_M3_StyleBar(lv_obj_t *bar)
{
	if (bar == NULL)
	{
		return;
	}

	lv_obj_set_style_radius(bar, GUI_M3_RADIUS_FULL, LV_PART_MAIN);
	lv_obj_set_style_bg_color(bar, GUI_Theme_LvColor(GUI_THEME_COLOR_SECONDARY_CONTAINER), LV_PART_MAIN);
	lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
	lv_obj_set_style_border_width(bar, 0, LV_PART_MAIN);

	lv_obj_set_style_radius(bar, GUI_M3_RADIUS_FULL, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(bar, GUI_Theme_LvColor(GUI_THEME_COLOR_PRIMARY), LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
}

void GUI_M3_StyleSpinner(lv_obj_t *spinner)
{
	if (spinner == NULL)
	{
		return;
	}

	lv_obj_set_style_arc_width(spinner, 4, LV_PART_MAIN);
	lv_obj_set_style_arc_color(spinner, GUI_Theme_LvColor(GUI_THEME_COLOR_SECONDARY_CONTAINER), LV_PART_MAIN);
	lv_obj_set_style_arc_width(spinner, 4, LV_PART_INDICATOR);
	lv_obj_set_style_arc_color(spinner, GUI_Theme_LvColor(GUI_THEME_COLOR_PRIMARY), LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(spinner, LV_OPA_TRANSP, LV_PART_KNOB);
	lv_obj_set_style_pad_all(spinner, 0, LV_PART_KNOB);
}

void GUI_M3_StyleTextPanel(lv_obj_t *obj)
{
	if (obj == NULL)
	{
		return;
	}

	lv_obj_set_style_radius(obj, GUI_M3_RADIUS_MD, 0);
	lv_obj_set_style_border_width(obj, 0, 0);
	lv_obj_set_style_shadow_width(obj, 0, 0);
	lv_obj_set_style_bg_color(obj, GUI_Theme_LvColor(GUI_THEME_COLOR_SURFACE_CONTAINER_LOW), 0);
	lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
	lv_obj_set_style_text_color(obj, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE), 0);
	lv_obj_set_style_pad_all(obj, GUI_M3_PAD_CARD, 0);
}
