#ifndef GUI_THEME_H
#define GUI_THEME_H

/*
 * Star Gamer GUI theme - Material Design 3 (Material You) design system.
 *
 * The module exposes three layers:
 *   1. Color roles     - MD3 baseline tonal scheme (light + dark) as color tokens.
 *   2. Design tokens    - MD3 shape / spacing / typography / motion / state layers.
 *   3. Component API    - factory helpers that build MD3 shaped widgets on LVGL 8.
 *
 * Legacy tokens (GUI_THEME_COLOR_SCREEN_BG ... GUI_THEME_COLOR_VIEWER_BG) are kept
 * so existing pages keep compiling; they are now mapped onto MD3 color roles.
 */

#include "sys.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------- */
/* Color roles                                                                */
/* ------------------------------------------------------------------------- */

typedef enum
{
	/* --- legacy aliases (mapped onto MD3 roles) --- */
	GUI_THEME_COLOR_SCREEN_BG = 0,
	GUI_THEME_COLOR_PANEL_BG,
	GUI_THEME_COLOR_PANEL_BORDER,
	GUI_THEME_COLOR_HEADER_LINE,
	GUI_THEME_COLOR_TOOL_BG,
	GUI_THEME_COLOR_TOOL_BG_PRESSED,
	GUI_THEME_COLOR_ROW_BG,
	GUI_THEME_COLOR_ROW_BG_PRESSED,
	GUI_THEME_COLOR_ROW_BG_FOCUSED,
	GUI_THEME_COLOR_ROW_BG_DISABLED,
	GUI_THEME_COLOR_TEXT_PRIMARY,
	GUI_THEME_COLOR_TEXT_NORMAL,
	GUI_THEME_COLOR_TEXT_SECONDARY,
	GUI_THEME_COLOR_TEXT_MUTED,
	GUI_THEME_COLOR_TEXT_DISABLED,
	GUI_THEME_COLOR_TEXT_FOCUSED,
	GUI_THEME_COLOR_SWITCH_BG,
	GUI_THEME_COLOR_SWITCH_KNOB,
	GUI_THEME_COLOR_SLIDER_BG,
	GUI_THEME_COLOR_VIEWER_BG,

	/* --- MD3 color roles --- */
	GUI_THEME_COLOR_PRIMARY,
	GUI_THEME_COLOR_ON_PRIMARY,
	GUI_THEME_COLOR_PRIMARY_CONTAINER,
	GUI_THEME_COLOR_ON_PRIMARY_CONTAINER,
	GUI_THEME_COLOR_SECONDARY,
	GUI_THEME_COLOR_ON_SECONDARY,
	GUI_THEME_COLOR_SECONDARY_CONTAINER,
	GUI_THEME_COLOR_ON_SECONDARY_CONTAINER,
	GUI_THEME_COLOR_TERTIARY,
	GUI_THEME_COLOR_ON_TERTIARY,
	GUI_THEME_COLOR_TERTIARY_CONTAINER,
	GUI_THEME_COLOR_ON_TERTIARY_CONTAINER,
	GUI_THEME_COLOR_ERROR,
	GUI_THEME_COLOR_ON_ERROR,
	GUI_THEME_COLOR_ERROR_CONTAINER,
	GUI_THEME_COLOR_ON_ERROR_CONTAINER,
	GUI_THEME_COLOR_SURFACE,
	GUI_THEME_COLOR_ON_SURFACE,
	GUI_THEME_COLOR_SURFACE_VARIANT,
	GUI_THEME_COLOR_ON_SURFACE_VARIANT,
	GUI_THEME_COLOR_SURFACE_DIM,
	GUI_THEME_COLOR_SURFACE_BRIGHT,
	GUI_THEME_COLOR_SURFACE_CONTAINER_LOWEST,
	GUI_THEME_COLOR_SURFACE_CONTAINER_LOW,
	GUI_THEME_COLOR_SURFACE_CONTAINER,
	GUI_THEME_COLOR_SURFACE_CONTAINER_HIGH,
	GUI_THEME_COLOR_SURFACE_CONTAINER_HIGHEST,
	GUI_THEME_COLOR_OUTLINE,
	GUI_THEME_COLOR_OUTLINE_VARIANT,
	GUI_THEME_COLOR_INVERSE_SURFACE,
	GUI_THEME_COLOR_INVERSE_ON_SURFACE,
	GUI_THEME_COLOR_INVERSE_PRIMARY,
	GUI_THEME_COLOR_SCRIM,

	GUI_THEME_COLOR_MAX
} GUI_ThemeColor_t;

/* ------------------------------------------------------------------------- */
/* Shape scale (MD3 corner radii, scaled for a 320x240 panel)                 */
/* ------------------------------------------------------------------------- */

#define GUI_M3_RADIUS_NONE            0
#define GUI_M3_RADIUS_XS              4
#define GUI_M3_RADIUS_SM              8
#define GUI_M3_RADIUS_MD              12
#define GUI_M3_RADIUS_LG              16
#define GUI_M3_RADIUS_XL              20
#define GUI_M3_RADIUS_FULL            LV_RADIUS_CIRCLE

/* ------------------------------------------------------------------------- */
/* Layout metrics                                                             */
/* ------------------------------------------------------------------------- */

#define GUI_M3_SCREEN_W               320
#define GUI_M3_SCREEN_H               240
#define GUI_M3_APPBAR_H               40
#define GUI_M3_ICON_BTN_SIZE          32
#define GUI_M3_ICON_BTN_GAP           4
#define GUI_M3_PAD_SCREEN             10
#define GUI_M3_PAD_CARD               8
#define GUI_M3_LIST_ROW_H             32

/* ------------------------------------------------------------------------- */
/* Motion (MD3 short/medium durations - deliberately minimal for the MCU)     */
/* ------------------------------------------------------------------------- */

#define GUI_M3_MOTION_SHORT           90U
#define GUI_M3_MOTION_MEDIUM          140U
#define GUI_M3_MOTION_LONG            200U
#define GUI_M3_MOTION_PAGE            140U

/* ------------------------------------------------------------------------- */
/* State layer opacities (MD3 spec)                                           */
/* ------------------------------------------------------------------------- */

#define GUI_M3_STATE_HOVER_OPA        ((lv_opa_t)20)   /* ~8 %  */
#define GUI_M3_STATE_FOCUS_OPA        ((lv_opa_t)26)   /* ~10 % */
#define GUI_M3_STATE_PRESSED_OPA      ((lv_opa_t)26)   /* ~10 % */
#define GUI_M3_STATE_DRAGGED_OPA      ((lv_opa_t)41)   /* ~16 % */
#define GUI_M3_DISABLED_CONTENT_OPA   ((lv_opa_t)97)   /* ~38 % */
#define GUI_M3_DISABLED_CONTAINER_OPA ((lv_opa_t)31)   /* ~12 % */

/* ------------------------------------------------------------------------- */
/* Material Symbols Rounded icons (dedicated LVGL icon fonts)                 */
/* ------------------------------------------------------------------------- */

LV_FONT_DECLARE(lv_font_md_icon_20)
LV_FONT_DECLARE(lv_font_md_icon_28)

#define GUI_ICON_ARROW_BACK    "\xEE\x97\x84"       /* U+E5C4 arrow_back */
#define GUI_ICON_REFRESH       "\xEE\x97\x95"       /* U+E5D5 refresh */
#define GUI_ICON_CLOSE         "\xEE\x97\x8D"       /* U+E5CD close */
#define GUI_ICON_CHECK         "\xEE\x99\xA8"       /* U+E668 check */
#define GUI_ICON_CHEVRON_RIGHT "\xEE\x97\x8C"       /* U+E5CC chevron_right */
#define GUI_ICON_ARROW_UPWARD  "\xEE\x97\x98"       /* U+E5D8 arrow_upward */
#define GUI_ICON_HOME          "\xEE\xA6\xB2"       /* U+E9B2 home */
#define GUI_ICON_GAMEPAD       "\xEE\xA8\xA8"       /* U+EA28 sports_esports */
#define GUI_ICON_VIDEOGAME     "\xEE\x8C\xB8"       /* U+E338 videogame_asset */
#define GUI_ICON_MUSIC         "\xEE\x90\x85"       /* U+E405 music_note */
#define GUI_ICON_MOVIE         "\xEE\x90\x84"       /* U+E404 movie */
#define GUI_ICON_IMAGE         "\xEE\x8F\xB4"       /* U+E3F4 image */
#define GUI_ICON_DESCRIPTION   "\xEE\xA1\xB3"       /* U+E873 description */
#define GUI_ICON_MEMORY        "\xEE\x8C\xA2"       /* U+E322 memory */
#define GUI_ICON_SETTINGS      "\xEE\xA2\xB8"       /* U+E8B8 settings */
#define GUI_ICON_FONT          "\xEE\x85\xA7"       /* U+E167 font_download */
#define GUI_ICON_FOLDER        "\xEE\x8B\x87"       /* U+E2C7 folder */
#define GUI_ICON_MORE_HORIZ    "\xEE\x97\x93"       /* U+E5D3 more_horiz */
#define GUI_ICON_PLAY          "\xEE\x80\xB7"       /* U+E037 play_arrow */
#define GUI_ICON_PAUSE         "\xEE\x80\xB4"       /* U+E034 pause */
#define GUI_ICON_STOP          "\xEE\x81\x87"       /* U+E047 stop */
#define GUI_ICON_SKIP_NEXT     "\xEE\x81\x84"       /* U+E044 skip_next */
#define GUI_ICON_SKIP_PREV     "\xEE\x81\x85"       /* U+E045 skip_previous */
#define GUI_ICON_VOLUME_UP     "\xEE\x81\x90"       /* U+E050 volume_up */
#define GUI_ICON_VOLUME_OFF    "\xEE\x81\x8F"       /* U+E04F volume_off */
#define GUI_ICON_DARK_MODE     "\xEE\x94\x9C"       /* U+E51C dark_mode */
#define GUI_ICON_LIGHT_MODE    "\xEE\x94\x98"       /* U+E518 light_mode */
#define GUI_ICON_USB           "\xEE\x87\xA0"       /* U+E1E0 usb */
#define GUI_ICON_SPEED         "\xEE\xA7\xA4"       /* U+E9E4 speed */
#define GUI_ICON_TUNE          "\xEE\x90\xA9"       /* U+E429 tune */
#define GUI_ICON_INFO          "\xEE\xA2\x8E"       /* U+E88E info */
#define GUI_ICON_DELETE        "\xEE\xA4\xAE"       /* U+E92E delete */
#define GUI_ICON_DOWNLOAD      "\xEF\x82\x90"       /* U+F090 download */
#define GUI_ICON_UPLOAD        "\xEF\x82\x9B"       /* U+F09B upload */
#define GUI_ICON_PALETTE       "\xEE\x90\x8A"       /* U+E40A palette */
#define GUI_ICON_TERMINAL      "\xEE\xAE\x8E"       /* U+EB8E terminal */
#define GUI_ICON_BUG           "\xEE\xA1\xA8"       /* U+E868 bug_report */
#define GUI_ICON_STORAGE       "\xEE\x87\x9B"       /* U+E1DB storage */
#define GUI_ICON_SD_CARD       "\xEE\x98\xA3"       /* U+E623 sd_card */
#define GUI_ICON_WARNING       "\xEF\x82\x83"       /* U+F083 warning */
#define GUI_ICON_ERROR         "\xEF\xA2\xB6"       /* U+F8B6 error */
#define GUI_ICON_CHECK_CIRCLE  "\xEF\x82\xBE"       /* U+F0BE check_circle */
#define GUI_ICON_FOLDER_OPEN   "\xEE\x8B\x88"       /* U+E2C8 folder_open */
#define GUI_ICON_TEXT_FIELDS   "\xEE\x89\xA2"       /* U+E262 text_fields */
#define GUI_ICON_ZOOM_IN       "\xEE\xA3\xBF"       /* U+E8FF zoom_in */
#define GUI_ICON_ZOOM_OUT      "\xEE\xA4\x80"       /* U+E900 zoom_out */
#define GUI_ICON_FULLSCREEN    "\xEE\x97\x90"       /* U+E5D0 fullscreen */
#define GUI_ICON_PLAY_CIRCLE   "\xEE\x87\x84"       /* U+E1C4 play_circle */
#define GUI_ICON_DEV_BOARD     "\xEE\x8C\x8D"       /* U+E30D developer_board */
#define GUI_ICON_RESTART       "\xEF\x81\x93"       /* U+F053 restart_alt */
#define GUI_ICON_APPS          "\xEE\x97\x83"       /* U+E5C3 apps */
#define GUI_ICON_GRAPHIC_EQ    "\xEE\x86\xB8"       /* U+E1B8 graphic_eq */
#define GUI_ICON_MORE_VERT     "\xEE\x97\x94"       /* U+E5D4 more_vert */
#define GUI_ICON_EXPAND_MORE   "\xEE\x97\x8F"       /* U+E5CF expand_more */
#define GUI_ICON_CODE          "\xEE\xA1\xAF"       /* U+E86F code */
#define GUI_ICON_AUTORENEW     "\xEE\xA1\xA3"       /* U+E863 autorenew */
#define GUI_ICON_BRIGHTNESS    "\xEE\x86\xAE"       /* U+E1AE brightness_medium */
#define GUI_ICON_BATTERY       "\xEE\x86\xA5"       /* U+E1A5 battery_full */

/* ------------------------------------------------------------------------- */
/* Typography roles                                                           */
/* ------------------------------------------------------------------------- */

typedef enum
{
	GUI_M3_TYPE_HEADLINE = 0,   /* page / dialog headline           */
	GUI_M3_TYPE_TITLE,          /* app bar title, card title        */
	GUI_M3_TYPE_BODY,           /* default reading text             */
	GUI_M3_TYPE_LABEL,          /* buttons, captions, supporting    */
	GUI_M3_TYPE_MAX
} GUI_M3_TypeRole_t;

/* ------------------------------------------------------------------------- */
/* Component variants                                                         */
/* ------------------------------------------------------------------------- */

typedef enum
{
	GUI_M3_BUTTON_FILLED = 0,   /* primary container, on-primary label   */
	GUI_M3_BUTTON_TONAL,        /* secondary container                   */
	GUI_M3_BUTTON_OUTLINED,     /* transparent + outline                 */
	GUI_M3_BUTTON_TEXT,         /* transparent, primary label            */
	GUI_M3_BUTTON_ELEVATED,     /* surface container low                 */
	GUI_M3_BUTTON_ICON,         /* 32x32 round, standard icon button     */
	GUI_M3_BUTTON_ICON_FILLED,  /* 32x32 round, filled icon button       */
	GUI_M3_BUTTON_ICON_TONAL,   /* 32x32 round, tonal icon button        */
	GUI_M3_BUTTON_FAB,          /* floating action button                */
	GUI_M3_BUTTON_ERROR         /* destructive filled button             */
} GUI_M3_ButtonStyle_t;

typedef enum
{
	GUI_M3_CARD_FILLED = 0,     /* surface container highest             */
	GUI_M3_CARD_ELEVATED,       /* surface container low                 */
	GUI_M3_CARD_OUTLINED        /* surface + outline variant border      */
} GUI_M3_CardStyle_t;

/* ------------------------------------------------------------------------- */
/* Theme core                                                                 */
/* ------------------------------------------------------------------------- */

int GUI_Theme_Apply(void);
u8 GUI_Theme_IsLight(void);
const char *GUI_Theme_Name(void);
u32 GUI_Theme_Color(GUI_ThemeColor_t color);
lv_color_t GUI_Theme_LvColor(GUI_ThemeColor_t color);

/* ------------------------------------------------------------------------- */
/* Design token helpers                                                       */
/* ------------------------------------------------------------------------- */

const lv_font_t *GUI_M3_Font(GUI_M3_TypeRole_t role);
const lv_font_t *GUI_M3_IconFont(void);
const lv_font_t *GUI_M3_IconFontLarge(void);
lv_color_t GUI_M3_StateLayer(GUI_ThemeColor_t base, GUI_ThemeColor_t overlay, lv_opa_t opa);

/* ------------------------------------------------------------------------- */
/* Component API                                                              */
/* ------------------------------------------------------------------------- */

void GUI_M3_ApplyScreen(lv_obj_t *screen);
void GUI_M3_ApplyFocusRing(lv_obj_t *obj, GUI_ThemeColor_t color);

lv_obj_t *GUI_M3_CreateTopAppBar(lv_obj_t *screen, const char *title);
lv_obj_t *GUI_M3_AppBarSetNav(lv_obj_t *bar, const char *icon, lv_event_cb_t cb, void *user_data);
lv_obj_t *GUI_M3_AppBarAddAction(lv_obj_t *bar, const char *icon, lv_event_cb_t cb, void *user_data);
lv_obj_t *GUI_M3_AppBarGetTitle(lv_obj_t *bar);

lv_obj_t *GUI_M3_CreateIconButton(lv_obj_t *parent, const char *icon, GUI_M3_ButtonStyle_t style);
lv_obj_t *GUI_M3_CreateButton(lv_obj_t *parent, GUI_M3_ButtonStyle_t style, const char *icon, const char *text);
void GUI_M3_StyleButton(lv_obj_t *btn, GUI_M3_ButtonStyle_t style);

lv_obj_t *GUI_M3_CreateCard(lv_obj_t *parent, GUI_M3_CardStyle_t style);
void GUI_M3_StyleCard(lv_obj_t *obj, GUI_M3_CardStyle_t style);

lv_obj_t *GUI_M3_CreateIconLabel(lv_obj_t *parent, const char *icon, GUI_ThemeColor_t color, u8 large);
lv_obj_t *GUI_M3_CreateIconContainer(lv_obj_t *parent, const char *icon, lv_coord_t size,
                                     GUI_ThemeColor_t bg, GUI_ThemeColor_t fg, u8 large_icon);
lv_obj_t *GUI_M3_CreateLabel(lv_obj_t *parent, const char *text, GUI_M3_TypeRole_t role, GUI_ThemeColor_t color);
lv_obj_t *GUI_M3_CreateDivider(lv_obj_t *parent, lv_coord_t x, lv_coord_t y, lv_coord_t w);

void GUI_M3_StyleList(lv_obj_t *list);
void GUI_M3_StyleListItem(lv_obj_t *item);
void GUI_M3_StyleSwitch(lv_obj_t *sw);
void GUI_M3_StyleSlider(lv_obj_t *slider);
void GUI_M3_StyleBar(lv_obj_t *bar);
void GUI_M3_StyleSpinner(lv_obj_t *spinner);
void GUI_M3_StyleTextPanel(lv_obj_t *obj);

#ifdef __cplusplus
}
#endif

#endif
