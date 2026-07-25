#include "GUI_Font.h"
#include "GUI_FileManage.h"
#include "GUI_Theme.h"
#include "System_Status.h"
#include "Malloc.h"
#include "w25q64.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "ff.h"

#include <stdio.h>
#include <string.h>

#define GUI_FONT_ACCENT                 0xF9A825U
#define GUI_FONT_XBF_HEADER_SIZE        8U
#define GUI_FONT_XBF_GLYPH_DSC_SIZE     6U
#define GUI_FONT_IO_CHUNK_SIZE          4096U
#define GUI_FONT_BITMAP_BUF_SIZE        1024U
#define GUI_FONT_INSTALL_STACK_SIZE     4096U
#define GUI_FONT_CLEANUP_DELAY_TICKS    2U
#define GUI_FONT_DEFAULT_LINE_HEIGHT    21U
#define GUI_FONT_DEFAULT_BASE_LINE      0U

typedef StaticTask_t osStaticThreadDef_t;

typedef enum
{
	GUI_FONT_STAGE_IDLE = 0,
	GUI_FONT_STAGE_VALIDATE,
	GUI_FONT_STAGE_ERASE,
	GUI_FONT_STAGE_WRITE,
	GUI_FONT_STAGE_VERIFY,
	GUI_FONT_STAGE_SAVE,
	GUI_FONT_STAGE_DONE,
	GUI_FONT_STAGE_ERROR
} GUI_FontStage_t;

typedef enum
{
	GUI_FONT_ERROR_NONE = 0,
	GUI_FONT_ERROR_OPEN,
	GUI_FONT_ERROR_SIZE,
	GUI_FONT_ERROR_READ,
	GUI_FONT_ERROR_FORMAT,
	GUI_FONT_ERROR_ERASE,
	GUI_FONT_ERROR_WRITE,
	GUI_FONT_ERROR_VERIFY,
	GUI_FONT_ERROR_SAVE,
	GUI_FONT_ERROR_MEMORY,
	GUI_FONT_ERROR_THREAD
} GUI_FontError_t;

typedef struct
{
	u16 min;
	u16 max;
	u8 bpp;
} GUI_FontXbfHeader_t;

typedef struct
{
	u8 adv_w;
	u8 box_w;
	u8 box_h;
	s8 ofs_x;
	s8 ofs_y;
	u8 reserved;
} GUI_FontGlyphDsc_t;

typedef struct
{
	GUI_FileManage_t file_manager;
	Page_Struct_t *page;
	lv_obj_t *progress_bar;
	lv_obj_t *progress_label;
	lv_timer_t *timer;
	char install_path[GUI_FILEMANAGE_PATH_SIZE];
	char status_text[72];
	u32 install_size;
	volatile u8 installing;
	volatile u8 thread_active;
	volatile u8 stage;
	volatile u8 error;
	volatile u8 progress;
	osThreadId_t install_thread;
	volatile u32 stack_space;
	volatile u32 exit_tick;
} GUI_FontContext_t;

static int GUI_Font_PageBuild(Page_Struct_t *page);
static int GUI_Font_PageLoaded(Page_Struct_t *page, void *user_data);
static GUI_FontContext_t *GUI_Font_GetContext(void);
static int GUI_Font_OpenFile(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data);
static int GUI_Font_Back(GUI_FileManage_t *manager, void *user_data);
static void GUI_Font_Timer(lv_timer_t *timer);
static void GUI_Font_InstallThread(void *argument);
static void GUI_Font_CleanupInstallThread(GUI_FontContext_t *context);
static u8 GUI_Font_EnsureIoBuffers(void);
static u8 GUI_Font_EnsureBitmapBuffer(void);
static u8 GUI_Font_EnsureInstallStack(void);
static void GUI_Font_WaitInputRelease(void);
static void GUI_Font_SetInstallState(GUI_FontContext_t *context, GUI_FontStage_t stage, u8 progress, GUI_FontError_t error);
static u8 GUI_Font_CopyText(char *dst, u32 dst_size, const char *src);
static u8 GUI_Font_ParseHeader(const u8 *data, GUI_FontXbfHeader_t *header);
static u8 GUI_Font_ValidateHeader(const GUI_FontXbfHeader_t *header, u32 file_size);
static u8 GUI_Font_ReadHeader(FIL *file, GUI_FontXbfHeader_t *header);
static u8 GUI_Font_InstallSaveSettings(const GUI_FontXbfHeader_t *header, u32 file_size);
static u8 GUI_Font_ProgressScale(u32 value, u32 total, u8 base, u8 span);
static const char *GUI_Font_ErrorText(u8 error);
static const char *GUI_Font_StageText(u8 stage);
static u16 GUI_Font_ReadLe16(const u8 *data);
static u32 GUI_Font_ReadLe32(const u8 *data);
static u8 GUI_Font_ReadGlyphOffset(uint32_t unicode_letter, u32 *glyph_offset);
static u8 GUI_Font_ReadGlyphDsc(u32 glyph_offset, GUI_FontGlyphDsc_t *glyph);
static const uint8_t *GUI_Font_GetGlyphBitmap(const lv_font_t *font, uint32_t unicode_letter);
static bool GUI_Font_GetGlyphDsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next);

static const char * const s_font_extensions[] = {".bin", ".xbf"};
static GUI_FontContext_t *s_font;
static u8 *s_font_io_buffer;
static u8 *s_font_verify_buffer;
static u8 *s_font_bitmap_buffer;
static osStaticThreadDef_t s_font_install_control_block;

static osThreadAttr_t s_font_install_thread_attr =
{
	.name = "FontInstall",
	.cb_mem = &s_font_install_control_block,
	.cb_size = sizeof(s_font_install_control_block),
	.stack_mem = NULL,
	.stack_size = GUI_FONT_INSTALL_STACK_SIZE,
	.priority = (osPriority_t)osPriorityBelowNormal,
};

static lv_font_t s_xbf_font =
{
	.get_glyph_dsc = GUI_Font_GetGlyphDsc,
	.get_glyph_bitmap = GUI_Font_GetGlyphBitmap,
	.line_height = GUI_FONT_DEFAULT_LINE_HEIGHT,
	.base_line = GUI_FONT_DEFAULT_BASE_LINE,
	.subpx = LV_FONT_SUBPX_NONE,
	.underline_position = 0,
	.underline_thickness = 0,
	.dsc = NULL,
	.fallback = LV_FONT_DEFAULT,
};

int GUI_Font_Register(void)
{
	if (Page_Find(GUI_PAGE_FONT) != NULL)
	{
		return SYSTEM_STATUS_OK;
	}

	if (GUI_Font_GetContext() == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	return Page_Create(GUI_PAGE_FONT, "Font Install", GUI_Font_PageBuild, GUI_Font_PageLoaded);
}

int GUI_Font_Show(void)
{
	int code;

	if (Page_Find(GUI_PAGE_FONT) == NULL)
	{
		code = GUI_Font_Register();
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	return Page_Show_Anim(GUI_PAGE_FONT, LV_SCR_LOAD_ANIM_MOVE_LEFT, 120U, 0U);
}

int GUI_Font_Uninstall(void)
{
	GUI_FontContext_t *context = s_font;
	System_Settings_t previous_settings;

	if (context != NULL)
	{
		GUI_Font_CleanupInstallThread(context);
		if (context->installing != 0U || context->thread_active != 0U || context->install_thread != NULL)
		{
			return OPERATE_FAILED;
		}
	}

	if (s_font_install_thread_attr.stack_mem != NULL)
	{
		return OPERATE_FAILED;
	}

	if (GUI_Font_IsInstalled() == 0U)
	{
		return SYSTEM_STATUS_OK;
	}

	previous_settings = System_Settings;
	System_Settings.FontInstalled = 0U;
	System_Settings.FontBpp = 0U;
	System_Settings.FontLineHeight = GUI_FONT_DEFAULT_LINE_HEIGHT;
	System_Settings.FontBaseLine = GUI_FONT_DEFAULT_BASE_LINE;
	System_Settings.FontFlashAddress = SYSTEM_FONT_FLASH_ADDR;
	System_Settings.FontSize = 0U;
	System_Settings.FontMinUnicode = 0U;
	System_Settings.FontMaxUnicode = 0U;

	if (System_Settings_Save() != SYSTEM_STATUS_OK)
	{
		System_Settings = previous_settings;
		return OPERATE_FAILED;
	}

	/* A later install erases the reserved region; avoid an extra NOR erase cycle here. */
	s_xbf_font.line_height = GUI_FONT_DEFAULT_LINE_HEIGHT;
	s_xbf_font.base_line = GUI_FONT_DEFAULT_BASE_LINE;
	return SYSTEM_STATUS_OK;
}

u8 GUI_Font_IsInstalled(void)
{
	return System_Settings_FontInstalled();
}

const lv_font_t* GUI_Font_GetXbfFont(void)
{
	if (GUI_Font_IsInstalled() == 0U)
	{
		return NULL;
	}

	s_xbf_font.line_height = (lv_coord_t)System_Settings.FontLineHeight;
	s_xbf_font.base_line = (lv_coord_t)System_Settings.FontBaseLine;
	return &s_xbf_font;
}

static int GUI_Font_PageBuild(Page_Struct_t *page)
{
	GUI_FontContext_t *context;
	GUI_FileManageConfig_t config;
	int code;

	if (page == NULL || page->screen == NULL)
	{
		return OPERATE_FAILED;
	}

	context = GUI_Font_GetContext();
	if (context == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	memset(context, 0, sizeof(*context));
	context->page = page;
	context->stage = (u8)GUI_FONT_STAGE_IDLE;

	memset(&config, 0, sizeof(config));
	config.title = "Font Install";
	config.root_path = "0:";
	config.empty_text = "No font files";
	config.status_text = "Fonts";
	config.extensions = s_font_extensions;
	config.extension_count = (u8)(sizeof(s_font_extensions) / sizeof(s_font_extensions[0]));
	config.accent_color = GUI_FONT_ACCENT;
	config.open = GUI_Font_OpenFile;
	config.back = GUI_Font_Back;
	config.user_data = context;

	code = GUI_FileManage_Create(&context->file_manager, page, &config);
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	lv_obj_set_pos(context->file_manager.status_label, 12, 224);

	context->progress_bar = lv_bar_create(page->screen);
	lv_obj_set_size(context->progress_bar, 236, 7);
	lv_obj_set_pos(context->progress_bar, 12, 212);
	lv_bar_set_range(context->progress_bar, 0, 100);
	lv_bar_set_value(context->progress_bar, 0, LV_ANIM_OFF);
	lv_obj_set_style_radius(context->progress_bar, 2, 0);
	lv_obj_set_style_bg_color(context->progress_bar, GUI_Theme_LvColor(GUI_THEME_COLOR_SLIDER_BG), 0);
	lv_obj_set_style_bg_opa(context->progress_bar, LV_OPA_COVER, 0);
	lv_obj_set_style_bg_color(context->progress_bar, lv_color_hex(GUI_FONT_ACCENT), LV_PART_INDICATOR);
	lv_obj_set_style_bg_opa(context->progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);

	context->progress_label = lv_label_create(page->screen);
	lv_label_set_text(context->progress_label, "0%");
	lv_obj_set_width(context->progress_label, 52);
	lv_obj_set_pos(context->progress_label, 258, 207);
	lv_obj_set_style_text_align(context->progress_label, LV_TEXT_ALIGN_RIGHT, 0);
	lv_obj_set_style_text_color(context->progress_label, GUI_Theme_LvColor(GUI_THEME_COLOR_TEXT_SECONDARY), 0);

	context->timer = lv_timer_create(GUI_Font_Timer, 100U, context);
	if (context->timer == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	if (GUI_Font_IsInstalled() != 0U)
	{
		GUI_FileManage_SetLocked(&context->file_manager, 0U);
		GUI_FileManage_SetStatus(&context->file_manager, "Font already installed");
		lv_bar_set_value(context->progress_bar, 100, LV_ANIM_OFF);
		lv_label_set_text(context->progress_label, "100%");
	}

	return SYSTEM_STATUS_OK;
}

static int GUI_Font_PageLoaded(Page_Struct_t *page, void *user_data)
{
	GUI_FontContext_t *context = s_font;
	int code;

	(void)page;
	(void)user_data;

	if (context == NULL)
	{
		return OPERATE_FAILED;
	}

	if (GUI_Font_IsInstalled() != 0U && context->installing == 0U)
	{
		GUI_FileManage_SetLocked(&context->file_manager, 0U);
		GUI_FileManage_SetStatus(&context->file_manager, "Font already installed");
	}
	else if (context->installing != 0U)
	{
		GUI_FileManage_SetLocked(&context->file_manager, 1U);
	}

	code = GUI_FileManage_PageLoaded(&context->file_manager);
	GUI_Font_WaitInputRelease();
	return code;
}

static GUI_FontContext_t *GUI_Font_GetContext(void)
{
	if (s_font == NULL)
	{
		s_font = (GUI_FontContext_t *)sys_malloc(SRAMEX, sizeof(GUI_FontContext_t));
		if (s_font != NULL)
		{
			memset(s_font, 0, sizeof(GUI_FontContext_t));
		}
	}

	return s_font;
}

static int GUI_Font_OpenFile(GUI_FileManage_t *manager, const GUI_FileManageItem_t *item, void *user_data)
{
	GUI_FontContext_t *context = (GUI_FontContext_t *)user_data;
	osThreadId_t tid;

	if (context == NULL || manager == NULL || item == NULL)
	{
		return OPERATE_FAILED;
	}

	if (GUI_Font_IsInstalled() != 0U)
	{
		GUI_FileManage_SetStatus(manager, "Font already installed");
		return SYSTEM_STATUS_OK;
	}

	GUI_Font_CleanupInstallThread(context);
	if (context->installing != 0U || context->thread_active != 0U || context->install_thread != NULL)
	{
		GUI_FileManage_SetStatus(manager, "Install is running");
		return OPERATE_FAILED;
	}

	if (item->size == 0U || item->size > SYSTEM_FONT_FLASH_SIZE)
	{
		GUI_FileManage_SetStatus(manager, "Font file is too large");
		return OPERATE_FAILED;
	}

	if (GUI_FileManage_BuildItemPath(manager, item, context->install_path, GUI_FILEMANAGE_PATH_SIZE) == 0U)
	{
		GUI_FileManage_SetStatus(manager, "Path too long");
		return OPERATE_FAILED;
	}

	if (GUI_Font_EnsureIoBuffers() == 0U || GUI_Font_EnsureBitmapBuffer() == 0U)
	{
		GUI_FileManage_SetStatus(manager, "Font memory error");
		return OUT_OF_HEAP_MEMORY;
	}

	if (GUI_Font_EnsureInstallStack() == 0U)
	{
		GUI_FileManage_SetStatus(manager, "Font thread stack error");
		return OUT_OF_HEAP_MEMORY;
	}

	context->install_size = item->size;
	context->installing = 1U;
	context->thread_active = 1U;
	context->stack_space = 0U;
	context->exit_tick = 0U;
	GUI_Font_SetInstallState(context, GUI_FONT_STAGE_VALIDATE, 0U, GUI_FONT_ERROR_NONE);
	GUI_FileManage_SetLocked(manager, 1U);
	GUI_FileManage_SetStatus(manager, "Validating font");

	tid = osThreadNew(GUI_Font_InstallThread, context, &s_font_install_thread_attr);
	if (tid == NULL)
	{
		sys_thread_stack_free(s_font_install_thread_attr.stack_mem);
		s_font_install_thread_attr.stack_mem = NULL;
		context->thread_active = 0U;
		context->installing = 0U;
		GUI_Font_SetInstallState(context, GUI_FONT_STAGE_ERROR, 0U, GUI_FONT_ERROR_THREAD);
		GUI_FileManage_SetLocked(manager, 0U);
		GUI_FileManage_SetStatus(manager, "Install thread failed");
		return OPERATE_FAILED;
	}

	context->install_thread = tid;
	return SYSTEM_STATUS_OK;
}

static int GUI_Font_Back(GUI_FileManage_t *manager, void *user_data)
{
	GUI_FontContext_t *context = (GUI_FontContext_t *)user_data;

	if (context != NULL)
	{
		GUI_Font_CleanupInstallThread(context);
	}

	if (context != NULL && (context->installing != 0U || context->install_thread != NULL))
	{
		if (manager != NULL)
		{
			GUI_FileManage_SetStatus(manager, "Install is running");
		}
		return OPERATE_FAILED;
	}

	return Page_Back();
}

static void GUI_Font_Timer(lv_timer_t *timer)
{
	GUI_FontContext_t *context = (GUI_FontContext_t *)timer->user_data;
	u8 stage;
	u8 progress;
	u8 error;

	if (context == NULL)
	{
		return;
	}

	GUI_Font_CleanupInstallThread(context);
	if (Page_Find(GUI_PAGE_FONT) == NULL)
	{
		return;
	}

	stage = context->stage;
	progress = context->progress;
	error = context->error;

	if (context->progress_bar != NULL)
	{
		lv_bar_set_value(context->progress_bar, progress, LV_ANIM_OFF);
	}
	if (context->progress_label != NULL)
	{
		(void)snprintf(context->status_text, sizeof(context->status_text), "%u%%", (unsigned int)progress);
		lv_label_set_text(context->progress_label, context->status_text);
	}

	if (stage == (u8)GUI_FONT_STAGE_ERROR)
	{
		context->installing = 0U;
		GUI_FileManage_SetLocked(&context->file_manager, 0U);
		(void)snprintf(context->status_text, sizeof(context->status_text), "Install failed: %s", GUI_Font_ErrorText(error));
		GUI_FileManage_SetStatus(&context->file_manager, context->status_text);
		GUI_Font_CleanupInstallThread(context);
	}
	else if (stage == (u8)GUI_FONT_STAGE_DONE)
	{
		u8 refresh_list = context->installing;

		context->installing = 0U;
		GUI_FileManage_SetLocked(&context->file_manager, 0U);
		if (refresh_list != 0U)
		{
			(void)GUI_FileManage_Refresh(&context->file_manager);
		}
		GUI_FileManage_SetStatus(&context->file_manager, "Font installed");
		GUI_Font_CleanupInstallThread(context);
	}
	else if (context->installing != 0U)
	{
		GUI_FileManage_SetLocked(&context->file_manager, 1U);
		GUI_FileManage_SetStatus(&context->file_manager, GUI_Font_StageText(stage));
	}
	else if (GUI_Font_IsInstalled() != 0U)
	{
		GUI_FileManage_SetLocked(&context->file_manager, 0U);
		GUI_FileManage_SetStatus(&context->file_manager, "Font already installed");
	}
}

static void GUI_Font_InstallThread(void *argument)
{
	GUI_FontContext_t *context = (GUI_FontContext_t *)argument;
	GUI_FontXbfHeader_t header;
	FIL *file = NULL;
	FRESULT fr;
	UINT br;
	u32 file_size = 0U;
	u32 address;
	u32 done;
	u32 chunk;
	u8 file_open = 0U;
	GUI_FontError_t error = GUI_FONT_ERROR_NONE;

	if (context == NULL)
	{
		osThreadExit();
		return;
	}

	memset(&header, 0, sizeof(header));

	/* FIL contains a sector cache used as an SDIO DMA target; keep it out of CCM. */
	file = (FIL *)sys_malloc(SRAMEX, sizeof(FIL));
	if (file == NULL)
	{
		error = GUI_FONT_ERROR_MEMORY;
		goto finish;
	}
	memset(file, 0, sizeof(*file));

	GUI_Font_SetInstallState(context, GUI_FONT_STAGE_VALIDATE, 1U, GUI_FONT_ERROR_NONE);
	fr = f_open(file, context->install_path, FA_READ);
	if (fr != FR_OK)
	{
		error = GUI_FONT_ERROR_OPEN;
		goto finish;
	}
	file_open = 1U;

	file_size = (u32)f_size(file);
	if (file_size == 0U || file_size > SYSTEM_FONT_FLASH_SIZE)
	{
		error = GUI_FONT_ERROR_SIZE;
		goto finish;
	}

	if (GUI_Font_ReadHeader(file, &header) == 0U || GUI_Font_ValidateHeader(&header, file_size) == 0U)
	{
		error = GUI_FONT_ERROR_FORMAT;
		goto finish;
	}

	fr = f_lseek(file, 0U);
	if (fr != FR_OK)
	{
		error = GUI_FONT_ERROR_READ;
		goto finish;
	}

	GUI_Font_SetInstallState(context, GUI_FONT_STAGE_ERASE, 5U, GUI_FONT_ERROR_NONE);
	for (address = SYSTEM_FONT_FLASH_ADDR; address < (SYSTEM_FONT_FLASH_ADDR + SYSTEM_FONT_FLASH_SIZE); address += W25Q64_BLOCK_64K_SIZE)
	{
		if (W25Q64_BlockErase64K(address) != W25Q64_OK)
		{
			error = GUI_FONT_ERROR_ERASE;
			goto finish;
		}
		GUI_Font_SetInstallState(context,
		                         GUI_FONT_STAGE_ERASE,
		                         GUI_Font_ProgressScale(address + W25Q64_BLOCK_64K_SIZE - SYSTEM_FONT_FLASH_ADDR,
		                                                SYSTEM_FONT_FLASH_SIZE,
		                                                5U,
		                                                25U),
		                         GUI_FONT_ERROR_NONE);
		osDelay(1U);
	}

	GUI_Font_SetInstallState(context, GUI_FONT_STAGE_WRITE, 30U, GUI_FONT_ERROR_NONE);
	done = 0U;
	while (done < file_size)
	{
		chunk = file_size - done;
		if (chunk > GUI_FONT_IO_CHUNK_SIZE)
		{
			chunk = GUI_FONT_IO_CHUNK_SIZE;
		}

		fr = f_read(file, s_font_io_buffer, (UINT)chunk, &br);
		if (fr != FR_OK || br != (UINT)chunk)
		{
			error = GUI_FONT_ERROR_READ;
			goto finish;
		}

		if (W25Q64_WriteData(SYSTEM_FONT_FLASH_ADDR + done, s_font_io_buffer, chunk) != W25Q64_OK)
		{
			error = GUI_FONT_ERROR_WRITE;
			goto finish;
		}

		done += chunk;
		GUI_Font_SetInstallState(context, GUI_FONT_STAGE_WRITE, GUI_Font_ProgressScale(done, file_size, 30U, 50U), GUI_FONT_ERROR_NONE);
		osDelay(1U);
	}

	fr = f_lseek(file, 0U);
	if (fr != FR_OK)
	{
		error = GUI_FONT_ERROR_READ;
		goto finish;
	}

	GUI_Font_SetInstallState(context, GUI_FONT_STAGE_VERIFY, 80U, GUI_FONT_ERROR_NONE);
	done = 0U;
	while (done < file_size)
	{
		chunk = file_size - done;
		if (chunk > GUI_FONT_IO_CHUNK_SIZE)
		{
			chunk = GUI_FONT_IO_CHUNK_SIZE;
		}

		fr = f_read(file, s_font_io_buffer, (UINT)chunk, &br);
		if (fr != FR_OK || br != (UINT)chunk)
		{
			error = GUI_FONT_ERROR_READ;
			goto finish;
		}

		if (W25Q64_ReadData(SYSTEM_FONT_FLASH_ADDR + done, s_font_verify_buffer, chunk) != W25Q64_OK ||
		    memcmp(s_font_io_buffer, s_font_verify_buffer, chunk) != 0)
		{
			error = GUI_FONT_ERROR_VERIFY;
			goto finish;
		}

		done += chunk;
		GUI_Font_SetInstallState(context, GUI_FONT_STAGE_VERIFY, GUI_Font_ProgressScale(done, file_size, 80U, 15U), GUI_FONT_ERROR_NONE);
		osDelay(1U);
	}

	GUI_Font_SetInstallState(context, GUI_FONT_STAGE_SAVE, 96U, GUI_FONT_ERROR_NONE);
	if (GUI_Font_InstallSaveSettings(&header, file_size) == 0U)
	{
		error = GUI_FONT_ERROR_SAVE;
		goto finish;
	}

finish:
	if (file_open != 0U)
	{
		(void)f_close(file);
	}
	if (file != NULL)
	{
		sys_free(SRAMEX, file);
	}

	if (error == GUI_FONT_ERROR_NONE)
	{
		GUI_Font_SetInstallState(context, GUI_FONT_STAGE_DONE, 100U, GUI_FONT_ERROR_NONE);
	}
	else
	{
		System_Settings.FontInstalled = 0U;
		System_Settings.FontSize = 0U;
		GUI_Font_SetInstallState(context, GUI_FONT_STAGE_ERROR, context->progress, error);
	}

	context->stack_space = (u32)uxTaskGetStackHighWaterMark(NULL) * (u32)sizeof(StackType_t);
	context->thread_active = 0U;
	__DMB();
	context->exit_tick = osKernelGetTickCount();
	__DMB();
	osThreadExit();
}

static void GUI_Font_CleanupInstallThread(GUI_FontContext_t *context)
{
	u32 now;

	if (context == NULL || context->install_thread == NULL || context->exit_tick == 0U)
	{
		return;
	}

	now = osKernelGetTickCount();
	if ((now - context->exit_tick) < GUI_FONT_CLEANUP_DELAY_TICKS)
	{
		return;
	}

	if (s_font_install_thread_attr.stack_mem != NULL)
	{
		sys_thread_stack_free(s_font_install_thread_attr.stack_mem);
		s_font_install_thread_attr.stack_mem = NULL;
	}
	context->install_thread = NULL;
	context->exit_tick = 0U;
}

static u8 GUI_Font_EnsureIoBuffers(void)
{
	if (s_font_io_buffer == NULL)
	{
		s_font_io_buffer = (u8 *)sys_malloc(SRAMEX, GUI_FONT_IO_CHUNK_SIZE);
	}
	if (s_font_verify_buffer == NULL)
	{
		s_font_verify_buffer = (u8 *)sys_malloc(SRAMEX, GUI_FONT_IO_CHUNK_SIZE);
	}

	if (s_font_io_buffer == NULL || s_font_verify_buffer == NULL)
	{
		if (s_font_io_buffer != NULL)
		{
			sys_free(SRAMEX, s_font_io_buffer);
			s_font_io_buffer = NULL;
		}
		if (s_font_verify_buffer != NULL)
		{
			sys_free(SRAMEX, s_font_verify_buffer);
			s_font_verify_buffer = NULL;
		}
		return 0U;
	}

	return 1U;
}

static u8 GUI_Font_EnsureBitmapBuffer(void)
{
	if (s_font_bitmap_buffer == NULL)
	{
		s_font_bitmap_buffer = (u8 *)sys_malloc(SRAMEX, GUI_FONT_BITMAP_BUF_SIZE);
	}

	return (s_font_bitmap_buffer != NULL) ? 1U : 0U;
}

static u8 GUI_Font_EnsureInstallStack(void)
{
	if (s_font_install_thread_attr.stack_mem == NULL)
	{
		s_font_install_thread_attr.stack_mem = sys_thread_stack_alloc((u32)s_font_install_thread_attr.stack_size);
	}

	return (s_font_install_thread_attr.stack_mem != NULL) ? 1U : 0U;
}

static void GUI_Font_WaitInputRelease(void)
{
	lv_indev_t *indev;

	indev = lv_indev_get_act();
	if (indev != NULL)
	{
		lv_indev_wait_release(indev);
	}
}

static void GUI_Font_SetInstallState(GUI_FontContext_t *context, GUI_FontStage_t stage, u8 progress, GUI_FontError_t error)
{
	if (context == NULL)
	{
		return;
	}

	if (progress > 100U)
	{
		progress = 100U;
	}

	context->progress = progress;
	context->error = (u8)error;
	context->stage = (u8)stage;
}

static u8 GUI_Font_CopyText(char *dst, u32 dst_size, const char *src)
{
	u32 len;

	if (dst == NULL || src == NULL || dst_size == 0U)
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

static u8 GUI_Font_ParseHeader(const u8 *data, GUI_FontXbfHeader_t *header)
{
	if (data == NULL || header == NULL)
	{
		return 0U;
	}

	header->min = GUI_Font_ReadLe16(&data[0]);
	header->max = GUI_Font_ReadLe16(&data[2]);
	header->bpp = data[4];

	return 1U;
}

static u8 GUI_Font_ValidateHeader(const GUI_FontXbfHeader_t *header, u32 file_size)
{
	u32 glyph_count;
	u32 table_size;

	if (header == NULL)
	{
		return 0U;
	}

	if (file_size < GUI_FONT_XBF_HEADER_SIZE || header->min > header->max)
	{
		return 0U;
	}

	if (header->bpp != 1U && header->bpp != 2U && header->bpp != 4U && header->bpp != 8U)
	{
		return 0U;
	}

	glyph_count = ((u32)header->max - (u32)header->min) + 1U;
	table_size = GUI_FONT_XBF_HEADER_SIZE + (glyph_count * 4U);
	if (file_size < table_size)
	{
		return 0U;
	}

	return 1U;
}

static u8 GUI_Font_ReadHeader(FIL *file, GUI_FontXbfHeader_t *header)
{
	FRESULT fr;
	UINT br;

	if (file == NULL || header == NULL)
	{
		return 0U;
	}

	fr = f_lseek(file, 0U);
	if (fr != FR_OK)
	{
		return 0U;
	}

	fr = f_read(file, s_font_io_buffer, GUI_FONT_XBF_HEADER_SIZE, &br);
	if (fr != FR_OK || br != GUI_FONT_XBF_HEADER_SIZE)
	{
		return 0U;
	}

	return GUI_Font_ParseHeader(s_font_io_buffer, header);
}

static u8 GUI_Font_InstallSaveSettings(const GUI_FontXbfHeader_t *header, u32 file_size)
{
	if (header == NULL)
	{
		return 0U;
	}

	System_Settings.FontInstalled = 1U;
	System_Settings.FontBpp = header->bpp;
	System_Settings.FontLineHeight = GUI_FONT_DEFAULT_LINE_HEIGHT;
	System_Settings.FontBaseLine = GUI_FONT_DEFAULT_BASE_LINE;
	System_Settings.FontFlashAddress = SYSTEM_FONT_FLASH_ADDR;
	System_Settings.FontSize = file_size;
	System_Settings.FontMinUnicode = header->min;
	System_Settings.FontMaxUnicode = header->max;

	return (System_Settings_Save() == SYSTEM_STATUS_OK) ? 1U : 0U;
}

static u8 GUI_Font_ProgressScale(u32 value, u32 total, u8 base, u8 span)
{
	u32 scaled;

	if (total == 0U)
	{
		return base;
	}

	if (value > total)
	{
		value = total;
	}

	scaled = (value * (u32)span) / total;
	if ((base + scaled) > 100U)
	{
		return 100U;
	}

	return (u8)(base + scaled);
}

static const char *GUI_Font_ErrorText(u8 error)
{
	switch (error)
	{
		case GUI_FONT_ERROR_OPEN:
			return "open file";
		case GUI_FONT_ERROR_SIZE:
			return "size";
		case GUI_FONT_ERROR_READ:
			return "read file";
		case GUI_FONT_ERROR_FORMAT:
			return "format";
		case GUI_FONT_ERROR_ERASE:
			return "erase flash";
		case GUI_FONT_ERROR_WRITE:
			return "write flash";
		case GUI_FONT_ERROR_VERIFY:
			return "verify";
		case GUI_FONT_ERROR_SAVE:
			return "save settings";
		case GUI_FONT_ERROR_MEMORY:
			return "memory";
		case GUI_FONT_ERROR_THREAD:
			return "thread";
		default:
			return "unknown";
	}
}

static const char *GUI_Font_StageText(u8 stage)
{
	switch (stage)
	{
		case GUI_FONT_STAGE_VALIDATE:
			return "Validating font";
		case GUI_FONT_STAGE_ERASE:
			return "Erasing flash";
		case GUI_FONT_STAGE_WRITE:
			return "Writing font";
		case GUI_FONT_STAGE_VERIFY:
			return "Verifying font";
		case GUI_FONT_STAGE_SAVE:
			return "Saving settings";
		case GUI_FONT_STAGE_DONE:
			return "Font installed";
		case GUI_FONT_STAGE_ERROR:
			return "Install failed";
		default:
			return "Ready";
	}
}

static u16 GUI_Font_ReadLe16(const u8 *data)
{
	return (u16)data[0] | ((u16)data[1] << 8);
}

static u32 GUI_Font_ReadLe32(const u8 *data)
{
	return (u32)data[0] |
	       ((u32)data[1] << 8) |
	       ((u32)data[2] << 16) |
	       ((u32)data[3] << 24);
}

static u8 GUI_Font_ReadGlyphOffset(uint32_t unicode_letter, u32 *glyph_offset)
{
	u8 table_data[4];
	u32 table_offset;
	u32 offset;

	if (glyph_offset == NULL || GUI_Font_IsInstalled() == 0U)
	{
		return 0U;
	}

	if (unicode_letter < (uint32_t)System_Settings.FontMinUnicode ||
	    unicode_letter > (uint32_t)System_Settings.FontMaxUnicode)
	{
		return 0U;
	}

	table_offset = GUI_FONT_XBF_HEADER_SIZE +
	               (((u32)unicode_letter - (u32)System_Settings.FontMinUnicode) * 4U);
	if ((table_offset + 4U) > System_Settings.FontSize)
	{
		return 0U;
	}

	if (W25Q64_ReadData(System_Settings.FontFlashAddress + table_offset, table_data, sizeof(table_data)) != W25Q64_OK)
	{
		return 0U;
	}

	offset = GUI_Font_ReadLe32(table_data);
	if (offset == 0U || (offset + GUI_FONT_XBF_GLYPH_DSC_SIZE) > System_Settings.FontSize)
	{
		return 0U;
	}

	*glyph_offset = offset;
	return 1U;
}

static u8 GUI_Font_ReadGlyphDsc(u32 glyph_offset, GUI_FontGlyphDsc_t *glyph)
{
	u8 data[GUI_FONT_XBF_GLYPH_DSC_SIZE];

	if (glyph == NULL || (glyph_offset + GUI_FONT_XBF_GLYPH_DSC_SIZE) > System_Settings.FontSize)
	{
		return 0U;
	}

	if (W25Q64_ReadData(System_Settings.FontFlashAddress + glyph_offset, data, sizeof(data)) != W25Q64_OK)
	{
		return 0U;
	}

	glyph->adv_w = data[0];
	glyph->box_w = data[1];
	glyph->box_h = data[2];
	glyph->ofs_x = (s8)data[3];
	glyph->ofs_y = (s8)data[4];
	glyph->reserved = data[5];

	return 1U;
}

static const uint8_t *GUI_Font_GetGlyphBitmap(const lv_font_t *font, uint32_t unicode_letter)
{
	GUI_FontGlyphDsc_t glyph;
	u32 glyph_offset;
	u32 bitmap_size;
	u32 bitmap_offset;

	(void)font;

	if (GUI_Font_ReadGlyphOffset(unicode_letter, &glyph_offset) == 0U ||
	    GUI_Font_ReadGlyphDsc(glyph_offset, &glyph) == 0U)
	{
		return NULL;
	}

	bitmap_size = (((u32)glyph.box_w * (u32)glyph.box_h * (u32)System_Settings.FontBpp) + 7U) / 8U;
	bitmap_offset = glyph_offset + GUI_FONT_XBF_GLYPH_DSC_SIZE;
	if (bitmap_size == 0U ||
	    bitmap_size > GUI_FONT_BITMAP_BUF_SIZE ||
	    (bitmap_offset + bitmap_size) > System_Settings.FontSize)
	{
		return NULL;
	}

	if (GUI_Font_EnsureBitmapBuffer() == 0U)
	{
		return NULL;
	}

	if (W25Q64_ReadData(System_Settings.FontFlashAddress + bitmap_offset, s_font_bitmap_buffer, bitmap_size) != W25Q64_OK)
	{
		return NULL;
	}

	return s_font_bitmap_buffer;
}

static bool GUI_Font_GetGlyphDsc(const lv_font_t *font, lv_font_glyph_dsc_t *dsc_out, uint32_t unicode_letter, uint32_t unicode_letter_next)
{
	GUI_FontGlyphDsc_t glyph;
	u32 glyph_offset;

	(void)font;
	(void)unicode_letter_next;

	if (dsc_out == NULL ||
	    GUI_Font_ReadGlyphOffset(unicode_letter, &glyph_offset) == 0U ||
	    GUI_Font_ReadGlyphDsc(glyph_offset, &glyph) == 0U)
	{
		return false;
	}

	dsc_out->adv_w = glyph.adv_w;
	dsc_out->box_w = glyph.box_w;
	dsc_out->box_h = glyph.box_h;
	dsc_out->ofs_x = glyph.ofs_x;
	dsc_out->ofs_y = glyph.ofs_y;
	dsc_out->bpp = System_Settings.FontBpp;
	dsc_out->is_placeholder = 0;

	return true;
}
