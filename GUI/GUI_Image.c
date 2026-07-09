#include "GUI_Image.h"
#include "GUI_FileManage.h"
#include "System_Status.h"
#include "Malloc.h"
#include "libjpeg.h"
#include "jerror.h"
#include "src/draw/lv_img_cache.h"

#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#define GUI_IMAGE_ACCENT             0x8E24AAU
#define GUI_IMAGE_MAX_W              320U
#define GUI_IMAGE_MAX_H              240U
#define GUI_IMAGE_SCREEN_W           320
#define GUI_IMAGE_SCREEN_H           240
#define GUI_IMAGE_CLOSE_W            34
#define GUI_IMAGE_CLOSE_H            26
#define GUI_IMAGE_STATUS_LEN         64U

typedef enum
{
	GUI_IMAGE_DECODE_OK = 0,
	GUI_IMAGE_DECODE_FILE,
	GUI_IMAGE_DECODE_FORMAT,
	GUI_IMAGE_DECODE_TOO_LARGE,
	GUI_IMAGE_DECODE_MEMORY
} GUI_ImageDecodeResult_t;

typedef struct
{
	u8 *data;
	u32 data_size;
	u16 width;
	u16 height;
	lv_img_dsc_t dsc;
} GUI_ImageDecoded_t;

typedef struct
{
	struct jpeg_error_mgr pub;
	jmp_buf jump_buffer;
	int msg_code;
	char message[JMSG_LENGTH_MAX];
} GUI_ImageJpegError_t;

typedef struct
{
	GUI_FileManage_t file_manage;
	lv_obj_t *viewer;
	lv_obj_t *image;
	lv_obj_t *close_btn;
	lv_obj_t *close_icon;
	lv_img_dsc_t image_dsc;
	u8 *image_data;
	u32 image_data_size;
	u16 image_w;
	u16 image_h;
	char current_name[GUI_FILEMANAGE_NAME_SIZE];
	char status_text[GUI_IMAGE_STATUS_LEN];
	u8 viewer_visible;
} GUI_ImageContext_t;

static int GUI_Image_PageBuild(Page_Struct_t *page);
static int GUI_Image_PageLoaded(Page_Struct_t *page, void *user_data);
static int GUI_Image_OpenFile(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data);
static int GUI_Image_Back(GUI_FileManage_t *manager, void *user_data);
static void GUI_Image_CreateViewer(GUI_ImageContext_t *context);
static void GUI_Image_CloseEvent(lv_event_t *event);
static void GUI_Image_ShowViewer(GUI_ImageContext_t *context);
static void GUI_Image_HideViewer(GUI_ImageContext_t *context);
static void GUI_Image_RebuildViewerFocus(GUI_ImageContext_t *context);
static void GUI_Image_WaitInputRelease(void);
static GUI_ImageDecodeResult_t GUI_Image_DecodeJpeg(const char *path, GUI_ImageDecoded_t *decoded);
static void GUI_Image_AdoptDecoded(GUI_ImageContext_t *context, GUI_ImageDecoded_t *decoded);
static void GUI_Image_FreeDecoded(GUI_ImageDecoded_t *decoded);
static void GUI_Image_FreeCurrent(GUI_ImageContext_t *context);
static void GUI_Image_ConvertRow(lv_color_t *dst, const u8 *src, u16 width);
static void GUI_Image_JpegErrorExit(j_common_ptr cinfo);
static void GUI_Image_JpegOutputMessage(j_common_ptr cinfo);
static const char *GUI_Image_ResultText(GUI_ImageDecodeResult_t result);
static u8 GUI_Image_CopyText(char *dst, u32 dst_size, const char *src);

static GUI_ImageContext_t s_image;
static const char * const s_image_extensions[] =
{
	".jpg",
	".jpeg"
};

int GUI_Image_Register(void)
{
	if (Page_Find(GUI_PAGE_IMAGE) != NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	return Page_Create(GUI_PAGE_IMAGE, "Image", GUI_Image_PageBuild, GUI_Image_PageLoaded);
}

int GUI_Image_Show(void)
{
	int code;

	if (Page_Find(GUI_PAGE_IMAGE) == NULL)
	{
		code = GUI_Image_Register();
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	return Page_Show_Anim(GUI_PAGE_IMAGE, LV_SCR_LOAD_ANIM_MOVE_LEFT, 120U, 0U);
}

static int GUI_Image_PageBuild(Page_Struct_t *page)
{
	GUI_FileManageConfig_t config;
	int code;

	if (page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}

	GUI_Image_FreeCurrent(&s_image);
	memset(&s_image, 0, sizeof(s_image));
	memset(&config, 0, sizeof(config));

	config.title = "Image";
	config.root_path = "0:/";
	config.empty_text = "No JPG files";
	config.status_text = "Image";
	config.extensions = s_image_extensions;
	config.extension_count = (u8)(sizeof(s_image_extensions) / sizeof(s_image_extensions[0]));
	config.accent_color = GUI_IMAGE_ACCENT;
	config.open = GUI_Image_OpenFile;
	config.back = GUI_Image_Back;
	config.user_data = &s_image;

	code = GUI_FileManage_Create(&s_image.file_manage, page, &config);
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	GUI_Image_CreateViewer(&s_image);
	return SYSTEM_STATUS_OK;
}

static int GUI_Image_PageLoaded(Page_Struct_t *page, void *user_data)
{
	(void)page;
	(void)user_data;

	if (s_image.viewer_visible != 0U)
	{
		GUI_Image_RebuildViewerFocus(&s_image);
		lv_obj_invalidate(s_image.viewer);
		return SYSTEM_STATUS_OK;
	}

	return GUI_FileManage_PageLoaded(&s_image.file_manage);
}

static int GUI_Image_OpenFile(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data)
{
	GUI_ImageContext_t *context = (GUI_ImageContext_t *)user_data;
	GUI_ImageDecoded_t decoded;
	GUI_ImageDecodeResult_t result;

	if (context == NULL || manager == NULL || item == NULL)
	{
		return OPERATE_FAILED;
	}

	memset(&decoded, 0, sizeof(decoded));
	GUI_FileManage_SetLocked(manager, 1U);
	GUI_FileManage_SetStatus(manager, "Decoding JPG");

	result = GUI_Image_DecodeJpeg(item->path, &decoded);
	if (result != GUI_IMAGE_DECODE_OK)
	{
		GUI_FileManage_SetLocked(manager, 0U);
		GUI_FileManage_SetStatus(manager, GUI_Image_ResultText(result));
		GUI_Image_FreeDecoded(&decoded);
		return (result == GUI_IMAGE_DECODE_MEMORY) ? OUT_OF_HEAP_MEMORY : OPERATE_FAILED;
	}

	GUI_Image_AdoptDecoded(context, &decoded);
	(void)GUI_Image_CopyText(context->current_name, GUI_FILEMANAGE_NAME_SIZE, item->name);
	(void)snprintf(context->status_text, sizeof(context->status_text),
	               "%s  %ux%u",
	               context->current_name,
	               (unsigned int)context->image_w,
	               (unsigned int)context->image_h);

	GUI_FileManage_SetLocked(manager, 0U);
	GUI_FileManage_SetStatus(manager, context->status_text);
	GUI_Image_ShowViewer(context);

	return SYSTEM_STATUS_OK;
}

static int GUI_Image_Back(GUI_FileManage_t *manager, void *user_data)
{
	GUI_ImageContext_t *context = (GUI_ImageContext_t *)user_data;

	(void)manager;

	if (context != NULL && context->viewer_visible != 0U)
	{
		GUI_Image_HideViewer(context);
		return SYSTEM_STATUS_OK;
	}

	return Page_Back();
}

static void GUI_Image_CreateViewer(GUI_ImageContext_t *context)
{
	if (context == NULL || context->file_manage.screen == NULL)
	{
		return;
	}

	context->viewer = lv_obj_create(context->file_manage.screen);
	lv_obj_clear_flag(context->viewer, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_add_flag(context->viewer, LV_OBJ_FLAG_CLICKABLE);
	lv_obj_set_size(context->viewer, GUI_IMAGE_SCREEN_W, GUI_IMAGE_SCREEN_H);
	lv_obj_set_pos(context->viewer, 0, 0);
	lv_obj_set_style_radius(context->viewer, 0, 0);
	lv_obj_set_style_border_width(context->viewer, 0, 0);
	lv_obj_set_style_bg_color(context->viewer, lv_color_black(), 0);
	lv_obj_set_style_bg_opa(context->viewer, LV_OPA_COVER, 0);
	lv_obj_set_style_pad_all(context->viewer, 0, 0);

	context->image = lv_img_create(context->viewer);
	lv_obj_center(context->image);

	context->close_btn = lv_btn_create(context->viewer);
	lv_obj_clear_flag(context->close_btn, LV_OBJ_FLAG_SCROLLABLE);
	lv_obj_set_size(context->close_btn, GUI_IMAGE_CLOSE_W, GUI_IMAGE_CLOSE_H);
	lv_obj_set_pos(context->close_btn, GUI_IMAGE_SCREEN_W - GUI_IMAGE_CLOSE_W - 6, 6);
	lv_obj_set_style_radius(context->close_btn, 6, 0);
	lv_obj_set_style_bg_color(context->close_btn, lv_color_hex(0x1D232AU), 0);
	lv_obj_set_style_bg_color(context->close_btn, lv_color_hex(0x333B45U), LV_STATE_PRESSED);
	lv_obj_set_style_bg_opa(context->close_btn, LV_OPA_80, 0);
	lv_obj_set_style_border_width(context->close_btn, 0, 0);
	lv_obj_set_style_border_width(context->close_btn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_border_color(context->close_btn, lv_color_hex(GUI_IMAGE_ACCENT), LV_STATE_FOCUSED);
	lv_obj_set_style_outline_width(context->close_btn, 2, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_pad(context->close_btn, 1, LV_STATE_FOCUSED);
	lv_obj_set_style_outline_color(context->close_btn, lv_color_hex(GUI_IMAGE_ACCENT), LV_STATE_FOCUSED);
	lv_obj_set_style_shadow_width(context->close_btn, 0, 0);
	lv_obj_set_style_pad_all(context->close_btn, 0, 0);
	lv_obj_add_event_cb(context->close_btn, GUI_Image_CloseEvent, LV_EVENT_CLICKED, context);
	lv_obj_add_event_cb(context->close_btn, GUI_Image_CloseEvent, LV_EVENT_KEY, context);

	context->close_icon = lv_label_create(context->close_btn);
	lv_label_set_text(context->close_icon, LV_SYMBOL_CLOSE);
	lv_obj_set_style_text_color(context->close_icon, lv_color_hex(0xF5F7FAU), 0);
	lv_obj_center(context->close_icon);

	lv_obj_add_flag(context->viewer, LV_OBJ_FLAG_HIDDEN);
}

static void GUI_Image_CloseEvent(lv_event_t *event)
{
	GUI_ImageContext_t *context = (GUI_ImageContext_t *)lv_event_get_user_data(event);
	lv_event_code_t code = lv_event_get_code(event);
	u32 key;

	if (context == NULL)
	{
		return;
	}

	if (code == LV_EVENT_CLICKED)
	{
		GUI_Image_HideViewer(context);
	}
	else if (code == LV_EVENT_KEY)
	{
		key = lv_event_get_key(event);
		if (key == LV_KEY_ENTER || key == LV_KEY_ESC || key == LV_KEY_LEFT)
		{
			GUI_Image_WaitInputRelease();
			GUI_Image_HideViewer(context);
		}
	}
}

static void GUI_Image_ShowViewer(GUI_ImageContext_t *context)
{
	if (context == NULL || context->viewer == NULL || context->image == NULL)
	{
		return;
	}

	if (context->image_data != NULL)
	{
		lv_img_cache_invalidate_src(&context->image_dsc);
	}
	lv_img_set_src(context->image, &context->image_dsc);
	lv_obj_center(context->image);
	lv_obj_clear_flag(context->viewer, LV_OBJ_FLAG_HIDDEN);
	lv_obj_move_foreground(context->viewer);
	context->viewer_visible = 1U;
	GUI_Image_RebuildViewerFocus(context);
	lv_obj_invalidate(context->viewer);
}

static void GUI_Image_HideViewer(GUI_ImageContext_t *context)
{
	if (context == NULL || context->viewer == NULL)
	{
		return;
	}

	lv_obj_add_flag(context->viewer, LV_OBJ_FLAG_HIDDEN);
	context->viewer_visible = 0U;

	if (Page_GetCurrent() == GUI_PAGE_IMAGE)
	{
		(void)GUI_FileManage_PageLoaded(&context->file_manage);
		lv_obj_invalidate(context->file_manage.screen);
	}
}

static void GUI_Image_RebuildViewerFocus(GUI_ImageContext_t *context)
{
	lv_group_t *group;

	if (context == NULL || context->close_btn == NULL)
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
	lv_group_add_obj(group, context->close_btn);
	lv_group_focus_obj(context->close_btn);
}

static void GUI_Image_WaitInputRelease(void)
{
	lv_indev_t *indev;

	indev = lv_indev_get_act();
	if (indev != NULL)
	{
		lv_indev_wait_release(indev);
	}
}

static GUI_ImageDecodeResult_t GUI_Image_DecodeJpeg(const char *path, GUI_ImageDecoded_t *decoded)
{
	FIL file;
	FRESULT fr;
	struct jpeg_decompress_struct cinfo;
	GUI_ImageJpegError_t jerr;
	JSAMPROW row_pointer[1];
	u8 * volatile row_buffer = NULL;
	u8 * volatile image_data = NULL;
	u32 row_stride;
	u32 data_size;
	u32 y;
	u16 width;
	u16 height;
	volatile u8 file_opened = 0U;
	volatile u8 jpeg_created = 0U;
	GUI_ImageDecodeResult_t result = GUI_IMAGE_DECODE_FORMAT;

	if (path == NULL || decoded == NULL)
	{
		return GUI_IMAGE_DECODE_FILE;
	}

	memset(decoded, 0, sizeof(*decoded));
	memset(&file, 0, sizeof(file));
	memset(&cinfo, 0, sizeof(cinfo));
	memset(&jerr, 0, sizeof(jerr));

	fr = f_open(&file, (const TCHAR *)path, FA_READ);
	if (fr != FR_OK)
	{
		return GUI_IMAGE_DECODE_FILE;
	}
	file_opened = 1U;

	cinfo.err = jpeg_std_error(&jerr.pub);
	jerr.pub.error_exit = GUI_Image_JpegErrorExit;
	jerr.pub.output_message = GUI_Image_JpegOutputMessage;

	if (setjmp(jerr.jump_buffer) != 0)
	{
		result = (jerr.msg_code == (int)JERR_OUT_OF_MEMORY) ? GUI_IMAGE_DECODE_MEMORY : GUI_IMAGE_DECODE_FORMAT;
		goto cleanup;
	}

	jpeg_create_decompress(&cinfo);
	jpeg_created = 1U;
	jpeg_stdio_src(&cinfo, &file);

	if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK)
	{
		result = GUI_IMAGE_DECODE_FORMAT;
		goto cleanup;
	}

	if (cinfo.image_width == 0U || cinfo.image_height == 0U ||
	    cinfo.image_width > GUI_IMAGE_MAX_W || cinfo.image_height > GUI_IMAGE_MAX_H)
	{
		result = GUI_IMAGE_DECODE_TOO_LARGE;
		goto cleanup;
	}

	cinfo.out_color_space = JCS_RGB;
	cinfo.dct_method = JDCT_IFAST;
	cinfo.do_fancy_upsampling = FALSE;

	if (jpeg_start_decompress(&cinfo) == FALSE)
	{
		result = GUI_IMAGE_DECODE_FORMAT;
		goto cleanup;
	}

	if (cinfo.output_width == 0U || cinfo.output_height == 0U ||
	    cinfo.output_width > GUI_IMAGE_MAX_W || cinfo.output_height > GUI_IMAGE_MAX_H ||
	    cinfo.output_components != 3)
	{
		result = GUI_IMAGE_DECODE_TOO_LARGE;
		goto cleanup;
	}

	width = (u16)cinfo.output_width;
	height = (u16)cinfo.output_height;
	row_stride = (u32)width * (u32)cinfo.output_components;
	data_size = (u32)width * (u32)height * (u32)sizeof(lv_color_t);

	image_data = (u8 *)sys_malloc(SRAMEX, data_size);
	if (image_data == NULL)
	{
		result = GUI_IMAGE_DECODE_MEMORY;
		goto cleanup;
	}

	row_buffer = (u8 *)sys_malloc(SRAMEX, row_stride);
	if (row_buffer == NULL)
	{
		result = GUI_IMAGE_DECODE_MEMORY;
		goto cleanup;
	}
	row_pointer[0] = (JSAMPROW)row_buffer;

	while (cinfo.output_scanline < cinfo.output_height)
	{
		y = (u32)cinfo.output_scanline;
		if (jpeg_read_scanlines(&cinfo, row_pointer, 1U) != 1U)
		{
			result = GUI_IMAGE_DECODE_FORMAT;
			goto cleanup;
		}
		GUI_Image_ConvertRow(&((lv_color_t *)image_data)[y * (u32)width], (const u8 *)row_buffer, width);
	}

	if (jpeg_finish_decompress(&cinfo) == FALSE)
	{
		result = GUI_IMAGE_DECODE_FORMAT;
		goto cleanup;
	}

	decoded->data = image_data;
	decoded->data_size = data_size;
	decoded->width = width;
	decoded->height = height;
	memset(&decoded->dsc, 0, sizeof(decoded->dsc));
	decoded->dsc.header.cf = LV_IMG_CF_TRUE_COLOR;
	decoded->dsc.header.w = width;
	decoded->dsc.header.h = height;
	decoded->dsc.data_size = data_size;
	decoded->dsc.data = image_data;
	image_data = NULL;
	result = GUI_IMAGE_DECODE_OK;

cleanup:
	if (row_buffer != NULL)
	{
		sys_free(SRAMEX, (void *)row_buffer);
	}
	if (image_data != NULL)
	{
		sys_free(SRAMEX, (void *)image_data);
	}
	if (jpeg_created != 0U)
	{
		jpeg_destroy_decompress(&cinfo);
	}
	if (file_opened != 0U)
	{
		(void)f_close(&file);
	}

	return result;
}

static void GUI_Image_AdoptDecoded(GUI_ImageContext_t *context, GUI_ImageDecoded_t *decoded)
{
	if (context == NULL || decoded == NULL || decoded->data == NULL)
	{
		return;
	}

	GUI_Image_FreeCurrent(context);
	context->image_data = decoded->data;
	context->image_data_size = decoded->data_size;
	context->image_w = decoded->width;
	context->image_h = decoded->height;
	context->image_dsc = decoded->dsc;

	decoded->data = NULL;
	decoded->data_size = 0U;
}

static void GUI_Image_FreeDecoded(GUI_ImageDecoded_t *decoded)
{
	if (decoded == NULL)
	{
		return;
	}

	if (decoded->data != NULL)
	{
		sys_free(SRAMEX, decoded->data);
	}
	memset(decoded, 0, sizeof(*decoded));
}

static void GUI_Image_FreeCurrent(GUI_ImageContext_t *context)
{
	if (context == NULL)
	{
		return;
	}

	if (context->image_data != NULL)
	{
		lv_img_cache_invalidate_src(&context->image_dsc);
	}
	if (context->image_data != NULL)
	{
		sys_free(SRAMEX, context->image_data);
	}

	context->image_data = NULL;
	context->image_data_size = 0U;
	context->image_w = 0U;
	context->image_h = 0U;
	memset(&context->image_dsc, 0, sizeof(context->image_dsc));
}

static void GUI_Image_ConvertRow(lv_color_t *dst, const u8 *src, u16 width)
{
	u16 x;

	if (dst == NULL || src == NULL)
	{
		return;
	}

	for (x = 0U; x < width; x++)
	{
		dst[x] = lv_color_make(src[0], src[1], src[2]);
		src += 3U;
	}
}

static void GUI_Image_JpegErrorExit(j_common_ptr cinfo)
{
	GUI_ImageJpegError_t *err = (GUI_ImageJpegError_t *)cinfo->err;

	if (err != NULL)
	{
		err->msg_code = cinfo->err->msg_code;
		if (cinfo->err->format_message != NULL)
		{
			(*cinfo->err->format_message)(cinfo, err->message);
		}
		longjmp(err->jump_buffer, 1);
	}

	while (1)
	{
	}
}

static void GUI_Image_JpegOutputMessage(j_common_ptr cinfo)
{
	(void)cinfo;
}

static const char *GUI_Image_ResultText(GUI_ImageDecodeResult_t result)
{
	switch (result)
	{
		case GUI_IMAGE_DECODE_OK:
			return "Image decoded";
		case GUI_IMAGE_DECODE_FILE:
			return "Open JPG failed";
		case GUI_IMAGE_DECODE_TOO_LARGE:
			return "JPG must fit 320x240";
		case GUI_IMAGE_DECODE_MEMORY:
			return "JPG memory error";
		case GUI_IMAGE_DECODE_FORMAT:
		default:
			return "JPG decode failed";
	}
}

static u8 GUI_Image_CopyText(char *dst, u32 dst_size, const char *src)
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
