#include "GUI_Text.h"
#include "GUI_FileManage.h"
#include "GUI_Font.h"
#include "GUI_Theme.h"
#include "System_Status.h"
#include "Malloc.h"
#include "ff.h"

#include <stdio.h>
#include <string.h>

#define GUI_TEXT_IO_BUFFER_SIZE         2048U
#define GUI_TEXT_RENDER_BUFFER_SIZE     8192U
#define GUI_TEXT_HISTORY_DEPTH          16U
#define GUI_TEXT_SCROLL_STEP            56
#define GUI_TEXT_STATUS_LEN             80U
#define GUI_TEXT_FALLBACK_UNICODE       0x53E3U

/* MD3 viewer metrics: the top app bar owns y = 0 .. GUI_M3_APPBAR_H - 1. */
#define GUI_TEXT_VIEWER_W               320
#define GUI_TEXT_VIEWER_H               240
#define GUI_TEXT_PANEL_X                10
#define GUI_TEXT_PANEL_Y                (GUI_M3_APPBAR_H + 6)
#define GUI_TEXT_PANEL_W                300
#define GUI_TEXT_PANEL_H                162
#define GUI_TEXT_LABEL_W                280
#define GUI_TEXT_STATUS_X               12
#define GUI_TEXT_STATUS_Y               216
#define GUI_TEXT_STATUS_W               296

#define GUI_TEXT_READ_OK                0U
#define GUI_TEXT_READ_EOF               1U
#define GUI_TEXT_READ_ERROR             2U

#define GUI_TEXT_DECODE_OK              0U
#define GUI_TEXT_DECODE_EOF             1U
#define GUI_TEXT_DECODE_INVALID         2U
#define GUI_TEXT_DECODE_FONT            3U

typedef enum
{
	GUI_TEXT_ENCODING_NONE = 0,
	GUI_TEXT_ENCODING_UTF8,
	GUI_TEXT_ENCODING_GBK
} GUI_TextEncoding_t;

typedef enum
{
	GUI_TEXT_OPEN_OK = 0,
	GUI_TEXT_OPEN_NO_FONT,
	GUI_TEXT_OPEN_MEMORY,
	GUI_TEXT_OPEN_FILE,
	GUI_TEXT_OPEN_READ,
	GUI_TEXT_OPEN_UNSUPPORTED_ENCODING,
	GUI_TEXT_OPEN_FONT_RANGE
} GUI_TextOpenResult_t;

typedef struct
{
	FIL *file;
	u8 *buffer;
	u32 buffer_size;
	u32 buffer_pos;
	u32 buffer_len;
	u32 file_size;
	u32 absolute_offset;
} GUI_TextReader_t;

typedef struct
{
	GUI_FileManage_t file_manage;
	Page_Struct_t *page;
	lv_obj_t *viewer;
	lv_obj_t *title_label;
	lv_obj_t *text_panel;
	lv_obj_t *text_label;
	lv_obj_t *status_label;
	lv_obj_t *back_btn;
	lv_obj_t *prev_btn;
	lv_obj_t *next_btn;
	u8 *io_buffer;
	char *render_buffer;
	char current_path[GUI_FILEMANAGE_PATH_SIZE];
	char current_name[GUI_FILEMANAGE_NAME_SIZE];
	u32 file_size;
	u32 text_start_offset;
	u32 page_offset;
	u32 next_offset;
	u32 history[GUI_TEXT_HISTORY_DEPTH];
	u8 history_count;
	u8 view_active;
	u8 encoding;
} GUI_TextContext_t;

static int GUI_Text_PageBuild(Page_Struct_t *page);
static int GUI_Text_PageLoaded(Page_Struct_t *page, void *user_data);
static GUI_TextContext_t *GUI_Text_GetContext(void);
static int GUI_Text_OpenFile(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data);
static int GUI_Text_Back(GUI_FileManage_t *manager, void *user_data);
static void GUI_Text_CreateViewer(GUI_TextContext_t *context);
static void GUI_Text_StylePagingButton(lv_obj_t *btn);
static void GUI_Text_BackEvent(lv_event_t *event);
static void GUI_Text_PrevEvent(lv_event_t *event);
static void GUI_Text_NextEvent(lv_event_t *event);
static void GUI_Text_ViewerEvent(lv_event_t *event);
static u8 GUI_Text_HandleKey(GUI_TextContext_t *context, u32 key);
static void GUI_Text_ShowViewer(GUI_TextContext_t *context);
static void GUI_Text_CloseViewer(GUI_TextContext_t *context);
static void GUI_Text_FocusViewer(GUI_TextContext_t *context);
static void GUI_Text_UpdateButtons(GUI_TextContext_t *context);
static void GUI_Text_UpdateStatus(GUI_TextContext_t *context, const char *override_text);
static int GUI_Text_NextPage(GUI_TextContext_t *context);
static int GUI_Text_PrevPage(GUI_TextContext_t *context);
static int GUI_Text_LoadPage(GUI_TextContext_t *context, u32 offset);
static GUI_TextOpenResult_t GUI_Text_PrepareFile(GUI_TextContext_t *context, const GUI_FileManageItem_t *item);
static GUI_TextOpenResult_t GUI_Text_DetectEncoding(const char *path, u32 *file_size, u32 *start_offset, GUI_TextEncoding_t *encoding);
static GUI_TextOpenResult_t GUI_Text_ValidateEncoding(const char *path, u32 file_size, u32 start_offset, GUI_TextEncoding_t encoding);
static u8 GUI_Text_EnsureBuffers(GUI_TextContext_t *context);
static FIL *GUI_Text_AllocFile(void);
static void GUI_Text_FreeFile(FIL *file);
static u8 GUI_Text_ReaderInit(GUI_TextReader_t *reader, FIL *file, u8 *buffer, u32 buffer_size, u32 file_size, u32 offset);
static u8 GUI_Text_ReadByte(GUI_TextReader_t *reader, u8 *value);
static u8 GUI_Text_DecodeNext(GUI_TextReader_t *reader, GUI_TextEncoding_t encoding, u32 *unicode);
static u8 GUI_Text_DecodeUtf8(GUI_TextReader_t *reader, u8 lead, u32 *unicode);
static u8 GUI_Text_DecodeGbk(GUI_TextReader_t *reader, u8 lead, u32 *unicode);
static u8 GUI_Text_ReadContinuation(GUI_TextReader_t *reader, u8 *value);
static u8 GUI_Text_UnicodeToUtf8(u32 unicode, u8 *dst, u8 *len);
static u8 GUI_Text_MatchGlyphOrFallback(u32 *unicode);
static u8 GUI_Text_FontSupports(u32 unicode);
static u8 GUI_Text_IsAllowedControl(u32 unicode);
static void GUI_Text_PushHistory(GUI_TextContext_t *context, u32 offset);
static const char *GUI_Text_EncodingName(u8 encoding);
static const char *GUI_Text_ResultText(GUI_TextOpenResult_t result);
static u8 GUI_Text_CopyText(char *dst, u32 dst_size, const char *src);

static GUI_TextContext_t *s_text;

static const char * const s_text_extensions[] =
{
	".txt",
	".log",
	".ini",
	".md",
	".c",
	".h",
	".cpp",
	".hpp",
	".json",
	".xml"
};

int GUI_Text_Register(void)
{
	if (Page_Find(GUI_PAGE_TEXT) != NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	if (GUI_Text_GetContext() == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	return Page_Create(GUI_PAGE_TEXT, "Text", GUI_Text_PageBuild, GUI_Text_PageLoaded);
}

int GUI_Text_Show(void)
{
	int code;

	if (Page_Find(GUI_PAGE_TEXT) == NULL)
	{
		code = GUI_Text_Register();
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	return Page_Show_Anim(GUI_PAGE_TEXT, LV_SCR_LOAD_ANIM_MOVE_LEFT, GUI_M3_MOTION_PAGE, 0U);
}

static int GUI_Text_PageBuild(Page_Struct_t *page)
{
	GUI_TextContext_t *context;
	GUI_FileManageConfig_t config;
	int code;

	if (page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}

	context = GUI_Text_GetContext();
	if (context == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	memset(context, 0, sizeof(*context));
	memset(&config, 0, sizeof(config));

	config.title = "Text";
	config.root_path = "0:/";
	config.empty_text = "No text files";
	config.extensions = s_text_extensions;
	config.extension_count = (u8)(sizeof(s_text_extensions) / sizeof(s_text_extensions[0]));
	config.accent_color = GUI_Theme_Color(GUI_THEME_COLOR_PRIMARY);
	config.open = GUI_Text_OpenFile;
	config.back = GUI_Text_Back;
	config.user_data = context;

	context->page = page;
	code = GUI_FileManage_Create(&context->file_manage, page, &config);
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	GUI_Text_CreateViewer(context);
	if (context->viewer == NULL || context->text_label == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	return SYSTEM_STATUS_OK;
}

static int GUI_Text_PageLoaded(Page_Struct_t *page, void *user_data)
{
	GUI_TextContext_t *context = s_text;

	(void)page;
	(void)user_data;

	if (context == NULL)
	{
		return OPERATE_FAILED;
	}

	if (context->view_active != 0U)
	{
		GUI_Text_FocusViewer(context);
		return SYSTEM_STATUS_OK;
	}

	if (GUI_Font_IsInstalled() == 0U)
	{
		(void)GUI_FileManage_ReportError(&context->file_manage, "Install font first");
	}

	return GUI_FileManage_PageLoaded(&context->file_manage);
}

static GUI_TextContext_t *GUI_Text_GetContext(void)
{
	if (s_text == NULL)
	{
		s_text = (GUI_TextContext_t *)sys_malloc(SRAMEX, sizeof(GUI_TextContext_t));
		if (s_text != NULL)
		{
			memset(s_text, 0, sizeof(GUI_TextContext_t));
		}
	}

	return s_text;
}

static int GUI_Text_OpenFile(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data)
{
	GUI_TextContext_t *context = (GUI_TextContext_t *)user_data;
	GUI_TextOpenResult_t result;

	if (context == NULL || manager == NULL || item == NULL)
	{
		return OPERATE_FAILED;
	}

	(void)GUI_FileManage_Notify(manager, "Checking text");
	GUI_FileManage_SetLocked(manager, 1U);

	result = GUI_Text_PrepareFile(context, item);
	if (result != GUI_TEXT_OPEN_OK)
	{
		GUI_FileManage_SetLocked(manager, 0U);
		(void)GUI_FileManage_ReportError(manager, GUI_Text_ResultText(result));
		return (result == GUI_TEXT_OPEN_MEMORY) ? OUT_OF_HEAP_MEMORY : OPERATE_FAILED;
	}

	if (GUI_Text_LoadPage(context, context->text_start_offset) != SYSTEM_STATUS_OK)
	{
		GUI_FileManage_SetLocked(manager, 0U);
		(void)GUI_FileManage_ReportError(manager, "Read text failed");
		return OPERATE_FAILED;
	}

	context->view_active = 1U;
	GUI_Text_ShowViewer(context);
	return SYSTEM_STATUS_OK;
}

static int GUI_Text_Back(GUI_FileManage_t *manager, void *user_data)
{
	(void)manager;
	(void)user_data;

	return Page_Back();
}

static void GUI_Text_CreateViewer(GUI_TextContext_t *context)
{
	lv_obj_t *bar;

	if (context == NULL || context->page == NULL || context->page->screen == NULL)
	{
		return;
	}

	/* MD3 full screen reading surface stacked over the file browser. */
	context->viewer = lv_obj_create(context->page->screen);
	if (context->viewer == NULL)
	{
		return;
	}
	GUI_M3_ApplyScreen(context->viewer);
	lv_obj_set_size(context->viewer, GUI_TEXT_VIEWER_W, GUI_TEXT_VIEWER_H);
	lv_obj_set_pos(context->viewer, 0, 0);

	/* MD3 top app bar: navigation icon plus the paging actions. */
	bar = GUI_M3_CreateTopAppBar(context->viewer, "Text");
	context->back_btn = GUI_M3_AppBarSetNav(bar, GUI_ICON_ARROW_BACK, GUI_Text_BackEvent, context);
	context->prev_btn = GUI_M3_AppBarAddAction(bar, GUI_ICON_ARROW_UPWARD, GUI_Text_PrevEvent, context);
	context->next_btn = GUI_M3_AppBarAddAction(bar, GUI_ICON_EXPAND_MORE, GUI_Text_NextEvent, context);
	GUI_Text_StylePagingButton(context->prev_btn);
	GUI_Text_StylePagingButton(context->next_btn);

	/* The app bar title doubles as the file name label (CJK capable font). */
	context->title_label = GUI_M3_AppBarGetTitle(bar);
	if (context->title_label == NULL)
	{
		return;
	}

	context->text_panel = lv_obj_create(context->viewer);
	if (context->text_panel == NULL)
	{
		return;
	}
	lv_obj_add_flag(context->text_panel, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(context->text_panel, GUI_TEXT_PANEL_W, GUI_TEXT_PANEL_H);
	lv_obj_set_pos(context->text_panel, GUI_TEXT_PANEL_X, GUI_TEXT_PANEL_Y);
	lv_obj_set_scroll_dir(context->text_panel, LV_DIR_VER);
	lv_obj_set_scrollbar_mode(context->text_panel, LV_SCROLLBAR_MODE_AUTO);
	GUI_M3_StyleTextPanel(context->text_panel);
	GUI_M3_ApplyFocusRing(context->text_panel, GUI_THEME_COLOR_PRIMARY);
	lv_obj_set_style_bg_color(context->text_panel, GUI_Theme_LvColor(GUI_THEME_COLOR_OUTLINE), LV_PART_SCROLLBAR);
	lv_obj_set_style_bg_opa(context->text_panel, LV_OPA_40, LV_PART_SCROLLBAR);
	lv_obj_set_style_radius(context->text_panel, GUI_M3_RADIUS_FULL, LV_PART_SCROLLBAR);
	lv_obj_set_style_width(context->text_panel, 4, LV_PART_SCROLLBAR);
	lv_obj_add_event_cb(context->text_panel, GUI_Text_ViewerEvent, LV_EVENT_KEY, context);

	context->text_label = lv_label_create(context->text_panel);
	if (context->text_label == NULL)
	{
		return;
	}
	lv_label_set_long_mode(context->text_label, LV_LABEL_LONG_WRAP);
	lv_obj_set_width(context->text_label, GUI_TEXT_LABEL_W);
	lv_obj_set_style_text_color(context->text_label, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE), 0);
	lv_obj_set_style_text_line_space(context->text_label, 2, 0);
	lv_label_set_text_static(context->text_label, "");

	context->status_label = lv_label_create(context->viewer);
	if (context->status_label == NULL)
	{
		return;
	}
	lv_label_set_long_mode(context->status_label, LV_LABEL_LONG_DOT);
	lv_obj_set_width(context->status_label, GUI_TEXT_STATUS_W);
	lv_obj_set_pos(context->status_label, GUI_TEXT_STATUS_X, GUI_TEXT_STATUS_Y);
	lv_obj_set_style_text_color(context->status_label, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE_VARIANT), 0);
	lv_label_set_text(context->status_label, "");

	lv_obj_add_flag(context->viewer, LV_OBJ_FLAG_HIDDEN);
}

/* MD3 disabled icon button: flat container, content dimmed to 38 %. */
static void GUI_Text_StylePagingButton(lv_obj_t *btn)
{
	if (btn == NULL)
	{
		return;
	}

	lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, LV_STATE_DISABLED);
	lv_obj_set_style_text_opa(btn, GUI_M3_DISABLED_CONTENT_OPA, LV_STATE_DISABLED);
	lv_obj_set_style_text_color(btn, GUI_Theme_LvColor(GUI_THEME_COLOR_ON_SURFACE), LV_STATE_DISABLED);
}

static void GUI_Text_BackEvent(lv_event_t *event)
{
	GUI_TextContext_t *context = (GUI_TextContext_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);

	if (context == NULL)
	{
		return;
	}

	if (code == LV_EVENT_CLICKED)
	{
		GUI_Text_CloseViewer(context);
	}
	else if (code == LV_EVENT_KEY)
	{
		(void)GUI_Text_HandleKey(context, lv_event_get_key(event));
	}
}

static void GUI_Text_PrevEvent(lv_event_t *event)
{
	GUI_TextContext_t *context = (GUI_TextContext_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);

	if (context == NULL)
	{
		return;
	}

	if (code == LV_EVENT_CLICKED)
	{
		(void)GUI_Text_PrevPage(context);
	}
	else if (code == LV_EVENT_KEY)
	{
		(void)GUI_Text_HandleKey(context, lv_event_get_key(event));
	}
}

static void GUI_Text_NextEvent(lv_event_t *event)
{
	GUI_TextContext_t *context = (GUI_TextContext_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);

	if (context == NULL)
	{
		return;
	}

	if (code == LV_EVENT_CLICKED)
	{
		(void)GUI_Text_NextPage(context);
	}
	else if (code == LV_EVENT_KEY)
	{
		(void)GUI_Text_HandleKey(context, lv_event_get_key(event));
	}
}

static void GUI_Text_ViewerEvent(lv_event_t *event)
{
	if (lv_event_get_code(event) == LV_EVENT_KEY)
	{
		(void)GUI_Text_HandleKey((GUI_TextContext_t *)lv_event_get_user_data(event), lv_event_get_key(event));
	}
}

static u8 GUI_Text_HandleKey(GUI_TextContext_t *context, u32 key)
{
	lv_coord_t scroll_y;

	if (context == NULL || context->view_active == 0U)
	{
		return 0U;
	}

	if (key == LV_KEY_ESC)
	{
		GUI_Text_CloseViewer(context);
		return 1U;
	}

	if (key == LV_KEY_RIGHT || key == LV_KEY_NEXT)
	{
		(void)GUI_Text_NextPage(context);
		return 1U;
	}

	if (key == LV_KEY_LEFT || key == LV_KEY_PREV)
	{
		(void)GUI_Text_PrevPage(context);
		return 1U;
	}

	if (context->text_panel == NULL)
	{
		return 0U;
	}

	if (key == LV_KEY_DOWN)
	{
		if (lv_obj_get_scroll_bottom(context->text_panel) <= 0 && context->next_offset > context->page_offset)
		{
			(void)GUI_Text_NextPage(context);
		}
		else
		{
			scroll_y = lv_obj_get_scroll_y(context->text_panel) + GUI_TEXT_SCROLL_STEP;
			lv_obj_scroll_to_y(context->text_panel, scroll_y, LV_ANIM_OFF);
		}
		return 1U;
	}

	if (key == LV_KEY_UP)
	{
		if (lv_obj_get_scroll_top(context->text_panel) <= 0 && context->history_count > 0U)
		{
			(void)GUI_Text_PrevPage(context);
		}
		else
		{
			scroll_y = lv_obj_get_scroll_y(context->text_panel) - GUI_TEXT_SCROLL_STEP;
			if (scroll_y < 0)
			{
				scroll_y = 0;
			}
			lv_obj_scroll_to_y(context->text_panel, scroll_y, LV_ANIM_OFF);
		}
		return 1U;
	}

	return 0U;
}

static void GUI_Text_ShowViewer(GUI_TextContext_t *context)
{
	const lv_font_t *font;

	if (context == NULL || context->viewer == NULL)
	{
		return;
	}

	font = GUI_Font_GetXbfFont();
	if (font != NULL)
	{
		if (context->title_label != NULL)
		{
			lv_obj_set_style_text_font(context->title_label, font, 0);
		}
		if (context->text_label != NULL)
		{
			lv_obj_set_style_text_font(context->text_label, font, 0);
		}
		if (context->status_label != NULL)
		{
			lv_obj_set_style_text_font(context->status_label, font, 0);
		}
	}

	lv_obj_clear_flag(context->viewer, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(context->viewer);
	if (context->title_label != NULL)
	{
		lv_label_set_text(context->title_label, context->current_name);
	}
	GUI_Text_UpdateButtons(context);
	GUI_Text_UpdateStatus(context, NULL);
	GUI_Text_FocusViewer(context);
	lv_obj_invalidate(context->viewer);
}

static void GUI_Text_CloseViewer(GUI_TextContext_t *context)
{
	if (context == NULL)
	{
		return;
	}

	context->view_active = 0U;
	if (context->viewer != NULL)
	{
		lv_obj_add_flag(context->viewer, LV_OBJ_FLAG_HIDDEN);
	}
	GUI_FileManage_SetLocked(&context->file_manage, 0U);
	(void)GUI_FileManage_Notify(&context->file_manage, "Text closed");
	(void)GUI_FileManage_PageLoaded(&context->file_manage);
}

static void GUI_Text_FocusViewer(GUI_TextContext_t *context)
{
	lv_group_t *group;

	if (context == NULL)
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

	if (context->back_btn != NULL)
	{
		lv_group_add_obj(group, context->back_btn);
	}
	if (context->prev_btn != NULL)
	{
		lv_group_add_obj(group, context->prev_btn);
	}
	if (context->next_btn != NULL)
	{
		lv_group_add_obj(group, context->next_btn);
	}
	if (context->text_panel != NULL)
	{
		lv_group_add_obj(group, context->text_panel);
		lv_group_focus_obj(context->text_panel);
	}
}

static void GUI_Text_UpdateButtons(GUI_TextContext_t *context)
{
	if (context == NULL)
	{
		return;
	}

	if (context->prev_btn != NULL)
	{
		if (context->history_count == 0U)
		{
			lv_obj_add_state(context->prev_btn, LV_STATE_DISABLED);
		}
		else
		{
			lv_obj_clear_state(context->prev_btn, LV_STATE_DISABLED);
		}
	}

	if (context->next_btn != NULL)
	{
		if (context->next_offset <= context->page_offset || context->next_offset >= context->file_size)
		{
			lv_obj_add_state(context->next_btn, LV_STATE_DISABLED);
		}
		else
		{
			lv_obj_clear_state(context->next_btn, LV_STATE_DISABLED);
		}
	}
}

static void GUI_Text_UpdateStatus(GUI_TextContext_t *context, const char *override_text)
{
	char text[GUI_TEXT_STATUS_LEN];
	u32 percent;

	if (context == NULL || context->status_label == NULL)
	{
		return;
	}

	if (override_text != NULL)
	{
		lv_label_set_text(context->status_label, override_text);
		return;
	}

	percent = (context->file_size > 0U) ? ((context->page_offset * 100U) / context->file_size) : 100U;
	if (percent > 100U)
	{
		percent = 100U;
	}

	(void)snprintf(text,
	               sizeof(text),
	               "%s  %lu%%  %lu/%lu",
	               GUI_Text_EncodingName(context->encoding),
	               (unsigned long)percent,
	               (unsigned long)context->page_offset,
	               (unsigned long)context->file_size);
	lv_label_set_text(context->status_label, text);
}

static int GUI_Text_NextPage(GUI_TextContext_t *context)
{
	u32 next;

	if (context == NULL || context->next_offset <= context->page_offset || context->next_offset >= context->file_size)
	{
		if (context != NULL)
		{
			GUI_Text_UpdateStatus(context, "End of file");
		}
		return OPERATE_FAILED;
	}

	next = context->next_offset;
	GUI_Text_PushHistory(context, context->page_offset);
	if (GUI_Text_LoadPage(context, next) != SYSTEM_STATUS_OK)
	{
		GUI_Text_UpdateStatus(context, "Read text failed");
		return OPERATE_FAILED;
	}

	GUI_Text_UpdateButtons(context);
	GUI_Text_UpdateStatus(context, NULL);
	return SYSTEM_STATUS_OK;
}

static int GUI_Text_PrevPage(GUI_TextContext_t *context)
{
	u32 offset;

	if (context == NULL || context->history_count == 0U)
	{
		if (context != NULL)
		{
			GUI_Text_UpdateStatus(context, "Top of file");
		}
		return OPERATE_FAILED;
	}

	context->history_count--;
	offset = context->history[context->history_count];
	if (GUI_Text_LoadPage(context, offset) != SYSTEM_STATUS_OK)
	{
		GUI_Text_UpdateStatus(context, "Read text failed");
		return OPERATE_FAILED;
	}

	GUI_Text_UpdateButtons(context);
	GUI_Text_UpdateStatus(context, NULL);
	return SYSTEM_STATUS_OK;
}

static int GUI_Text_LoadPage(GUI_TextContext_t *context, u32 offset)
{
	FIL *file;
	GUI_TextReader_t reader;
	FRESULT fr;
	u32 unicode;
	u32 char_start;
	u32 output_index = 0U;
	u8 utf8[3];
	u8 utf8_len;
	u8 status;
	u8 full = 0U;

	if (context == NULL || context->render_buffer == NULL || context->io_buffer == NULL)
	{
		return OPERATE_FAILED;
	}

	file = GUI_Text_AllocFile();
	if (file == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	fr = f_open(file, context->current_path, FA_READ);
	if (fr != FR_OK)
	{
		GUI_Text_FreeFile(file);
		return OPERATE_FAILED;
	}

	if (offset > context->file_size)
	{
		offset = context->text_start_offset;
	}

	if (GUI_Text_ReaderInit(&reader, file, context->io_buffer, GUI_TEXT_IO_BUFFER_SIZE, context->file_size, offset) == 0U)
	{
		(void)f_close(file);
		GUI_Text_FreeFile(file);
		return OPERATE_FAILED;
	}

	while (output_index < (GUI_TEXT_RENDER_BUFFER_SIZE - 4U))
	{
		char_start = reader.absolute_offset;
		status = GUI_Text_DecodeNext(&reader, (GUI_TextEncoding_t)context->encoding, &unicode);
		if (status == GUI_TEXT_DECODE_EOF)
		{
			break;
		}
		if (status == GUI_TEXT_DECODE_FONT)
		{
			status = GUI_TEXT_DECODE_OK;
		}
		if (status != GUI_TEXT_DECODE_OK)
		{
			(void)f_close(file);
			GUI_Text_FreeFile(file);
			return OPERATE_FAILED;
		}

		if (unicode == '\r')
		{
			continue;
		}

		if (unicode == '\t')
		{
			unicode = ' ';
		}

		if (GUI_Text_UnicodeToUtf8(unicode, utf8, &utf8_len) == 0U)
		{
			(void)f_close(file);
			GUI_Text_FreeFile(file);
			return OPERATE_FAILED;
		}

		if ((output_index + (u32)utf8_len) >= (GUI_TEXT_RENDER_BUFFER_SIZE - 1U))
		{
			context->next_offset = char_start;
			full = 1U;
			break;
		}

		memcpy(&context->render_buffer[output_index], utf8, utf8_len);
		output_index += utf8_len;
	}

	if (full == 0U)
	{
		context->next_offset = reader.absolute_offset;
	}

	context->render_buffer[output_index] = '\0';
	if (output_index == 0U)
	{
		(void)GUI_Text_CopyText(context->render_buffer, GUI_TEXT_RENDER_BUFFER_SIZE, "(empty file)");
		context->next_offset = context->file_size;
	}

	(void)f_close(file);
	GUI_Text_FreeFile(file);

	context->page_offset = offset;
	if (context->text_label != NULL)
	{
		lv_label_set_text_static(context->text_label, context->render_buffer);
	}
	if (context->text_panel != NULL)
	{
		lv_obj_scroll_to_y(context->text_panel, 0, LV_ANIM_OFF);
		lv_obj_update_layout(context->text_panel);
	}

	return SYSTEM_STATUS_OK;
}

static GUI_TextOpenResult_t GUI_Text_PrepareFile(GUI_TextContext_t *context, const GUI_FileManageItem_t *item)
{
	GUI_TextOpenResult_t result;
	GUI_TextEncoding_t encoding = GUI_TEXT_ENCODING_NONE;
	u32 file_size = 0U;
	u32 start_offset = 0U;

	if (context == NULL || item == NULL)
	{
		return GUI_TEXT_OPEN_FILE;
	}

	if (GUI_Font_IsInstalled() == 0U || GUI_Font_GetXbfFont() == NULL)
	{
		return GUI_TEXT_OPEN_NO_FONT;
	}

	if (GUI_Text_EnsureBuffers(context) == 0U)
	{
		return GUI_TEXT_OPEN_MEMORY;
	}

	if (GUI_FileManage_BuildItemPath(&context->file_manage, item, context->current_path, GUI_FILEMANAGE_PATH_SIZE) == 0U ||
	    GUI_Text_CopyText(context->current_name, GUI_FILEMANAGE_NAME_SIZE, item->name) == 0U)
	{
		return GUI_TEXT_OPEN_FILE;
	}

	result = GUI_Text_DetectEncoding(context->current_path, &file_size, &start_offset, &encoding);
	if (result != GUI_TEXT_OPEN_OK)
	{
		return result;
	}

	context->file_size = file_size;
	context->text_start_offset = start_offset;
	context->page_offset = start_offset;
	context->next_offset = start_offset;
	context->history_count = 0U;
	context->encoding = (u8)encoding;

	return GUI_TEXT_OPEN_OK;
}

static GUI_TextOpenResult_t GUI_Text_DetectEncoding(const char *path, u32 *file_size, u32 *start_offset, GUI_TextEncoding_t *encoding)
{
	FIL *file;
	FRESULT fr;
	UINT br = 0U;
	u8 prefix[4] = {0U, 0U, 0U, 0U};
	GUI_TextOpenResult_t result;
	u32 size;

	if (path == NULL || file_size == NULL || start_offset == NULL || encoding == NULL)
	{
		return GUI_TEXT_OPEN_FILE;
	}

	file = GUI_Text_AllocFile();
	if (file == NULL)
	{
		return GUI_TEXT_OPEN_MEMORY;
	}

	fr = f_open(file, path, FA_READ);
	if (fr != FR_OK)
	{
		GUI_Text_FreeFile(file);
		return GUI_TEXT_OPEN_FILE;
	}

	size = (u32)f_size(file);
	if (size > 0U)
	{
		fr = f_read(file, prefix, (size >= sizeof(prefix)) ? sizeof(prefix) : (UINT)size, &br);
		if (fr != FR_OK)
		{
			(void)f_close(file);
			GUI_Text_FreeFile(file);
			return GUI_TEXT_OPEN_READ;
		}
	}

	(void)f_close(file);
	GUI_Text_FreeFile(file);

	*file_size = size;
	*start_offset = 0U;
	*encoding = GUI_TEXT_ENCODING_UTF8;

	if (size == 0U)
	{
		return GUI_TEXT_OPEN_OK;
	}

	if ((br >= 2U && ((prefix[0] == 0xFFU && prefix[1] == 0xFEU) ||
	                  (prefix[0] == 0xFEU && prefix[1] == 0xFFU))) ||
	    (br >= 4U && ((prefix[0] == 0x00U && prefix[1] == 0x00U && prefix[2] == 0xFEU && prefix[3] == 0xFFU) ||
	                  (prefix[0] == 0xFFU && prefix[1] == 0xFEU && prefix[2] == 0x00U && prefix[3] == 0x00U))))
	{
		return GUI_TEXT_OPEN_UNSUPPORTED_ENCODING;
	}

	if (br >= 3U && prefix[0] == 0xEFU && prefix[1] == 0xBBU && prefix[2] == 0xBFU)
	{
		*start_offset = 3U;
		result = GUI_Text_ValidateEncoding(path, size, *start_offset, GUI_TEXT_ENCODING_UTF8);
		if (result == GUI_TEXT_OPEN_OK)
		{
			*encoding = GUI_TEXT_ENCODING_UTF8;
		}
		return result;
	}

	result = GUI_Text_ValidateEncoding(path, size, 0U, GUI_TEXT_ENCODING_UTF8);
	if (result == GUI_TEXT_OPEN_OK)
	{
		*encoding = GUI_TEXT_ENCODING_UTF8;
		return GUI_TEXT_OPEN_OK;
	}

	result = GUI_Text_ValidateEncoding(path, size, 0U, GUI_TEXT_ENCODING_GBK);
	if (result == GUI_TEXT_OPEN_OK)
	{
		*encoding = GUI_TEXT_ENCODING_GBK;
		return GUI_TEXT_OPEN_OK;
	}

	return result;
}

static GUI_TextOpenResult_t GUI_Text_ValidateEncoding(const char *path, u32 file_size, u32 start_offset, GUI_TextEncoding_t encoding)
{
	FIL *file;
	GUI_TextReader_t reader;
	FRESULT fr;
	u32 unicode;
	u8 status;
	GUI_TextOpenResult_t result = GUI_TEXT_OPEN_OK;

	file = GUI_Text_AllocFile();
	if (file == NULL)
	{
		return GUI_TEXT_OPEN_MEMORY;
	}

	fr = f_open(file, path, FA_READ);
	if (fr != FR_OK)
	{
		GUI_Text_FreeFile(file);
		return GUI_TEXT_OPEN_FILE;
	}

	if (s_text == NULL || s_text->io_buffer == NULL)
	{
		(void)f_close(file);
		GUI_Text_FreeFile(file);
		return GUI_TEXT_OPEN_MEMORY;
	}

	if (GUI_Text_ReaderInit(&reader, file, s_text->io_buffer, GUI_TEXT_IO_BUFFER_SIZE, file_size, start_offset) == 0U)
	{
		(void)f_close(file);
		GUI_Text_FreeFile(file);
		return GUI_TEXT_OPEN_READ;
	}

	while (1)
	{
		status = GUI_Text_DecodeNext(&reader, encoding, &unicode);
		if (status == GUI_TEXT_DECODE_EOF)
		{
			break;
		}
		if (status == GUI_TEXT_DECODE_FONT)
		{
			status = GUI_TEXT_DECODE_OK;
		}
		if (status != GUI_TEXT_DECODE_OK)
		{
			result = GUI_TEXT_OPEN_UNSUPPORTED_ENCODING;
			break;
		}
		if (unicode == 0U || (unicode < 0x20U && GUI_Text_IsAllowedControl(unicode) == 0U))
		{
			result = GUI_TEXT_OPEN_UNSUPPORTED_ENCODING;
			break;
		}
	}

	(void)f_close(file);
	GUI_Text_FreeFile(file);
	return result;
}

static u8 GUI_Text_EnsureBuffers(GUI_TextContext_t *context)
{
	if (context == NULL)
	{
		return 0U;
	}

	if (context->io_buffer == NULL)
	{
		context->io_buffer = (u8 *)sys_malloc(SRAMEX, GUI_TEXT_IO_BUFFER_SIZE);
	}
	if (context->render_buffer == NULL)
	{
		context->render_buffer = (char *)sys_malloc(SRAMEX, GUI_TEXT_RENDER_BUFFER_SIZE);
	}

	if (context->io_buffer == NULL || context->render_buffer == NULL)
	{
		if (context->io_buffer != NULL)
		{
			sys_free(SRAMEX, context->io_buffer);
			context->io_buffer = NULL;
		}
		if (context->render_buffer != NULL)
		{
			sys_free(SRAMEX, context->render_buffer);
			context->render_buffer = NULL;
		}
		return 0U;
	}

	context->render_buffer[0] = '\0';
	return 1U;
}

static FIL *GUI_Text_AllocFile(void)
{
	FIL *file;

	file = (FIL *)sys_malloc(SRAMEX, sizeof(FIL));
	if (file != NULL)
	{
		memset(file, 0, sizeof(FIL));
	}

	return file;
}

static void GUI_Text_FreeFile(FIL *file)
{
	if (file != NULL)
	{
		sys_free(SRAMEX, file);
	}
}

static u8 GUI_Text_ReaderInit(GUI_TextReader_t *reader, FIL *file, u8 *buffer, u32 buffer_size, u32 file_size, u32 offset)
{
	if (reader == NULL || file == NULL || buffer == NULL || buffer_size == 0U || offset > file_size)
	{
		return 0U;
	}

	if (f_lseek(file, offset) != FR_OK)
	{
		return 0U;
	}

	memset(reader, 0, sizeof(GUI_TextReader_t));
	reader->file = file;
	reader->buffer = buffer;
	reader->buffer_size = buffer_size;
	reader->file_size = file_size;
	reader->absolute_offset = offset;

	return 1U;
}

static u8 GUI_Text_ReadByte(GUI_TextReader_t *reader, u8 *value)
{
	UINT br;
	u32 remain;
	u32 chunk;

	if (reader == NULL || value == NULL)
	{
		return GUI_TEXT_READ_ERROR;
	}

	if (reader->buffer_pos >= reader->buffer_len)
	{
		if (reader->absolute_offset >= reader->file_size)
		{
			return GUI_TEXT_READ_EOF;
		}

		remain = reader->file_size - reader->absolute_offset;
		chunk = (remain > reader->buffer_size) ? reader->buffer_size : remain;
		if (f_read(reader->file, reader->buffer, (UINT)chunk, &br) != FR_OK)
		{
			return GUI_TEXT_READ_ERROR;
		}
		if (br == 0U)
		{
			return GUI_TEXT_READ_EOF;
		}

		reader->buffer_pos = 0U;
		reader->buffer_len = (u32)br;
	}

	*value = reader->buffer[reader->buffer_pos];
	reader->buffer_pos++;
	reader->absolute_offset++;
	return GUI_TEXT_READ_OK;
}

static u8 GUI_Text_DecodeNext(GUI_TextReader_t *reader, GUI_TextEncoding_t encoding, u32 *unicode)
{
	u8 lead;
	u8 status;

	if (unicode == NULL)
	{
		return GUI_TEXT_DECODE_INVALID;
	}

	status = GUI_Text_ReadByte(reader, &lead);
	if (status == GUI_TEXT_READ_EOF)
	{
		return GUI_TEXT_DECODE_EOF;
	}
	if (status != GUI_TEXT_READ_OK)
	{
		return GUI_TEXT_DECODE_INVALID;
	}

	if (encoding == GUI_TEXT_ENCODING_GBK)
	{
		return GUI_Text_DecodeGbk(reader, lead, unicode);
	}

	return GUI_Text_DecodeUtf8(reader, lead, unicode);
}

static u8 GUI_Text_DecodeUtf8(GUI_TextReader_t *reader, u8 lead, u32 *unicode)
{
	u8 c1;
	u8 c2;
	u8 c3;

	if (unicode == NULL)
	{
		return GUI_TEXT_DECODE_INVALID;
	}

	if (lead < 0x80U)
	{
		*unicode = lead;
		return GUI_Text_MatchGlyphOrFallback(unicode);
	}

	if (lead >= 0xC2U && lead <= 0xDFU)
	{
		if (GUI_Text_ReadContinuation(reader, &c1) == 0U)
		{
			return GUI_TEXT_DECODE_INVALID;
		}
		*unicode = (((u32)lead & 0x1FU) << 6U) | ((u32)c1 & 0x3FU);
		return GUI_Text_MatchGlyphOrFallback(unicode);
	}

	if (lead >= 0xE0U && lead <= 0xEFU)
	{
		if (GUI_Text_ReadContinuation(reader, &c1) == 0U ||
		    GUI_Text_ReadContinuation(reader, &c2) == 0U)
		{
			return GUI_TEXT_DECODE_INVALID;
		}
		if ((lead == 0xE0U && c1 < 0xA0U) || (lead == 0xEDU && c1 >= 0xA0U))
		{
			return GUI_TEXT_DECODE_INVALID;
		}
		*unicode = (((u32)lead & 0x0FU) << 12U) |
		           (((u32)c1 & 0x3FU) << 6U) |
		           ((u32)c2 & 0x3FU);
		return GUI_Text_MatchGlyphOrFallback(unicode);
	}

	if (lead >= 0xF0U && lead <= 0xF4U)
	{
		if (GUI_Text_ReadContinuation(reader, &c1) == 0U ||
		    GUI_Text_ReadContinuation(reader, &c2) == 0U ||
		    GUI_Text_ReadContinuation(reader, &c3) == 0U)
		{
			return GUI_TEXT_DECODE_INVALID;
		}
		if ((lead == 0xF0U && c1 < 0x90U) || (lead == 0xF4U && c1 >= 0x90U))
		{
			return GUI_TEXT_DECODE_INVALID;
		}
		*unicode = (((u32)lead & 0x07U) << 18U) |
		           (((u32)c1 & 0x3FU) << 12U) |
		           (((u32)c2 & 0x3FU) << 6U) |
		           ((u32)c3 & 0x3FU);
		return GUI_Text_MatchGlyphOrFallback(unicode);
	}

	return GUI_TEXT_DECODE_INVALID;
}

static u8 GUI_Text_DecodeGbk(GUI_TextReader_t *reader, u8 lead, u32 *unicode)
{
	u8 trail;
	u16 oem;

	if (unicode == NULL)
	{
		return GUI_TEXT_DECODE_INVALID;
	}

	if (lead < 0x80U)
	{
		*unicode = lead;
		return GUI_Text_MatchGlyphOrFallback(unicode);
	}

	if (lead < 0x81U || lead > 0xFEU)
	{
		return GUI_TEXT_DECODE_INVALID;
	}

	if (GUI_Text_ReadByte(reader, &trail) != GUI_TEXT_READ_OK)
	{
		return GUI_TEXT_DECODE_INVALID;
	}
	if (trail < 0x40U || trail > 0xFEU || trail == 0x7FU)
	{
		return GUI_TEXT_DECODE_INVALID;
	}

	oem = (u16)(((u16)lead << 8U) | (u16)trail);
	*unicode = (u32)ff_convert((WCHAR)oem, 1U);
	if (*unicode == 0U)
	{
		return GUI_TEXT_DECODE_INVALID;
	}

	return GUI_Text_MatchGlyphOrFallback(unicode);
}

static u8 GUI_Text_ReadContinuation(GUI_TextReader_t *reader, u8 *value)
{
	if (GUI_Text_ReadByte(reader, value) != GUI_TEXT_READ_OK)
	{
		return 0U;
	}

	return ((*value & 0xC0U) == 0x80U) ? 1U : 0U;
}

static u8 GUI_Text_UnicodeToUtf8(u32 unicode, u8 *dst, u8 *len)
{
	if (dst == NULL || len == NULL || unicode > 0xFFFFU)
	{
		return 0U;
	}

	if (unicode < 0x80U)
	{
		dst[0] = (u8)unicode;
		*len = 1U;
	}
	else if (unicode < 0x800U)
	{
		dst[0] = (u8)(0xC0U | (unicode >> 6U));
		dst[1] = (u8)(0x80U | (unicode & 0x3FU));
		*len = 2U;
	}
	else
	{
		dst[0] = (u8)(0xE0U | (unicode >> 12U));
		dst[1] = (u8)(0x80U | ((unicode >> 6U) & 0x3FU));
		dst[2] = (u8)(0x80U | (unicode & 0x3FU));
		*len = 3U;
	}

	return 1U;
}

static u8 GUI_Text_MatchGlyphOrFallback(u32 *unicode)
{
	if (unicode == NULL)
	{
		return GUI_TEXT_DECODE_INVALID;
	}

	if (GUI_Text_FontSupports(*unicode) != 0U)
	{
		return GUI_TEXT_DECODE_OK;
	}

	*unicode = GUI_TEXT_FALLBACK_UNICODE;
	return GUI_TEXT_DECODE_FONT;
}

static u8 GUI_Text_FontSupports(u32 unicode)
{
	if (GUI_Text_IsAllowedControl(unicode) != 0U)
	{
		return 1U;
	}

	if (unicode < 0x80U)
	{
		return 1U;
	}

	if (GUI_Font_IsInstalled() == 0U)
	{
		return 0U;
	}

	return (unicode >= (u32)System_Settings.FontMinUnicode &&
	        unicode <= (u32)System_Settings.FontMaxUnicode) ? 1U : 0U;
}

static u8 GUI_Text_IsAllowedControl(u32 unicode)
{
	return (unicode == '\r' || unicode == '\n' || unicode == '\t' || unicode == '\f') ? 1U : 0U;
}

static void GUI_Text_PushHistory(GUI_TextContext_t *context, u32 offset)
{
	u8 i;

	if (context == NULL)
	{
		return;
	}

	if (context->history_count >= GUI_TEXT_HISTORY_DEPTH)
	{
		for (i = 1U; i < GUI_TEXT_HISTORY_DEPTH; i++)
		{
			context->history[i - 1U] = context->history[i];
		}
		context->history_count = GUI_TEXT_HISTORY_DEPTH - 1U;
	}

	context->history[context->history_count] = offset;
	context->history_count++;
}

static const char *GUI_Text_EncodingName(u8 encoding)
{
	switch (encoding)
	{
		case GUI_TEXT_ENCODING_GBK:
			return "GBK";
		case GUI_TEXT_ENCODING_UTF8:
			return "UTF-8";
		default:
			return "Unknown";
	}
}

static const char *GUI_Text_ResultText(GUI_TextOpenResult_t result)
{
	switch (result)
	{
		case GUI_TEXT_OPEN_NO_FONT:
			return "Install font first";
		case GUI_TEXT_OPEN_MEMORY:
			return "Text memory error";
		case GUI_TEXT_OPEN_FILE:
			return "Open text failed";
		case GUI_TEXT_OPEN_READ:
			return "Read text failed";
		case GUI_TEXT_OPEN_FONT_RANGE:
			return "Font lacks glyphs";
		case GUI_TEXT_OPEN_UNSUPPORTED_ENCODING:
			return "Unsupported encoding";
		default:
			return "Text ready";
	}
}

static u8 GUI_Text_CopyText(char *dst, u32 dst_size, const char *src)
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
