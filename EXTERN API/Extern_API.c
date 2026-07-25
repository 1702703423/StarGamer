#include "Extern_API.h"

#include "LCD.h"
#include "Malloc.h"
#include "System_Status.h"
#include "key.h"

#define EXTERN_API_LCD_MAX_WIDTH 320U

static osThreadId_t s_axf_thread;
static osThreadId_t s_lcd_owner;
static volatile u8 s_exit_requested;
static u8 *s_lcd_line;

static u8 ExternAPI_IsCurrentAXF(void);
static u8 ExternAPI_IsLCDOwner(void);
static int ExternAPI_LCD_WriteColor(u16 width, u16 height, u16 color);
static int ExternAPI_LCD_WritePixels(u16 width, u16 height, const u16 *pixels);
static void ExternAPI_LCD_EncodePixel(u8 *dst, u16 color);

int ExternAPI_AXF_Begin(osThreadId_t tid)
{
	s32 lock_state;
	u8 *line;

	if (tid == NULL || tid != osThreadGetId())
	{
		return OPERATE_FAILED;
	}
	line = (u8 *)sys_malloc(SRAMEX, EXTERN_API_LCD_MAX_WIDTH * 2U);
	if (line == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	lock_state = osKernelLock();
	if (lock_state < 0)
	{
		sys_free(SRAMEX, line);
		return OPERATE_FAILED;
	}
	if (s_axf_thread != NULL || s_lcd_line != NULL)
	{
		(void)osKernelRestoreLock(lock_state);
		sys_free(SRAMEX, line);
		return OPERATE_FAILED;
	}

	s_axf_thread = tid;
	s_lcd_line = line;
	s_exit_requested = 0U;
	__DMB();
	(void)osKernelRestoreLock(lock_state);
	return SYSTEM_STATUS_OK;
}

void ExternAPI_AXF_RequestExit(osThreadId_t tid)
{
	if (tid != NULL && s_axf_thread == tid)
	{
		s_exit_requested = 1U;
		__DMB();
	}
}

int ExternAPI_AXF_End(osThreadId_t tid)
{
	s32 lock_state;
	int code = SYSTEM_STATUS_OK;
	u8 *line = NULL;

	if (tid == NULL || s_axf_thread != tid)
	{
		return OPERATE_FAILED;
	}
	if (s_lcd_owner == tid)
	{
		code = (tid == osThreadGetId()) ? ExternAPI_LCD_Release() :
		       ExternAPI_AXF_ForceEnd(tid);
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	lock_state = osKernelLock();
	if (lock_state < 0)
	{
		return OPERATE_FAILED;
	}
	if (s_axf_thread == tid)
	{
		s_axf_thread = NULL;
		s_exit_requested = 0U;
		line = s_lcd_line;
		s_lcd_line = NULL;
	}
	__DMB();
	(void)osKernelRestoreLock(lock_state);
	if (line != NULL)
	{
		sys_free(SRAMEX, line);
	}
	return SYSTEM_STATUS_OK;
}

int ExternAPI_AXF_ForceEnd(osThreadId_t tid)
{
	s32 lock_state;
	int code = SYSTEM_STATUS_OK;
	u8 *line = NULL;

	if (tid == NULL)
	{
		return OPERATE_FAILED;
	}

	if (s_lcd_owner == tid)
	{
		(void)LCD_StreamWait();
		LCD_StreamEnd();
		code = lvgl_recover_ui(tid);
		if (code != SYSTEM_STATUS_OK)
		{
			return code;
		}
	}

	lock_state = osKernelLock();
	if (lock_state < 0)
	{
		return OPERATE_FAILED;
	}
	if (s_lcd_owner == tid)
	{
		s_lcd_owner = NULL;
	}
	if (s_axf_thread == tid)
	{
		s_axf_thread = NULL;
		s_exit_requested = 0U;
		line = s_lcd_line;
		s_lcd_line = NULL;
	}
	__DMB();
	(void)osKernelRestoreLock(lock_state);
	if (line != NULL)
	{
		sys_free(SRAMEX, line);
	}
	return SYSTEM_STATUS_OK;
}

int ExternAPI_LCD_Acquire(void)
{
	osThreadId_t current;
	s32 lock_state;
	int code;

	current = osThreadGetId();
	if (ExternAPI_IsCurrentAXF() == 0U || s_lcd_owner != NULL)
	{
		return OPERATE_FAILED;
	}

	lock_state = osKernelLock();
	if (lock_state < 0)
	{
		return OPERATE_FAILED;
	}
	if (s_axf_thread != current || s_lcd_owner != NULL)
	{
		(void)osKernelRestoreLock(lock_state);
		return OPERATE_FAILED;
	}
	/* Reserve ownership before the blocking LVGL handshake for forced cleanup. */
	s_lcd_owner = current;
	__DMB();
	(void)osKernelRestoreLock(lock_state);

	code = lvgl_stop_ui();
	if (code != SYSTEM_STATUS_OK)
	{
		lock_state = osKernelLock();
		if (lock_state >= 0)
		{
			if (s_lcd_owner == current)
			{
				s_lcd_owner = NULL;
			}
			(void)osKernelRestoreLock(lock_state);
		}
		return code;
	}

	return SYSTEM_STATUS_OK;
}

int ExternAPI_LCD_Release(void)
{
	osThreadId_t current;
	s32 lock_state;
	int code;

	current = osThreadGetId();
	if (current == NULL || s_lcd_owner != current)
	{
		return OPERATE_FAILED;
	}

	(void)LCD_StreamWait();
	LCD_StreamEnd();
	code = lvgl_start_ui();
	if (code != SYSTEM_STATUS_OK)
	{
		code = lvgl_recover_ui(current);
	}
	if (code != SYSTEM_STATUS_OK)
	{
		return code;
	}

	lock_state = osKernelLock();
	if (lock_state < 0)
	{
		return OPERATE_FAILED;
	}
	if (s_lcd_owner == current)
	{
		s_lcd_owner = NULL;
	}
	__DMB();
	(void)osKernelRestoreLock(lock_state);
	return SYSTEM_STATUS_OK;
}

int ExternAPI_LCD_Clear(u16 color)
{
	return ExternAPI_LCD_FillRect(0U, 0U, lcddev.width, lcddev.height, color);
}

int ExternAPI_LCD_DrawPixel(u16 x, u16 y, u16 color)
{
	return ExternAPI_LCD_FillRect(x, y, 1U, 1U, color);
}

int ExternAPI_LCD_FillRect(u16 x, u16 y, u16 width, u16 height, u16 color)
{
	if (ExternAPI_IsLCDOwner() == 0U || width == 0U || height == 0U ||
	    width > EXTERN_API_LCD_MAX_WIDTH ||
	    (u32)x + width > lcddev.width || (u32)y + height > lcddev.height)
	{
		return OPERATE_FAILED;
	}
	if (LCD_StreamBegin(x, y, width, height) != 0U)
	{
		return OPERATE_FAILED;
	}

	return ExternAPI_LCD_WriteColor(width, height, color);
}

int ExternAPI_LCD_BlitRGB565(u16 x, u16 y, u16 width, u16 height,
	const u16 *pixels)
{
	if (ExternAPI_IsLCDOwner() == 0U || pixels == NULL || width == 0U ||
	    height == 0U || width > EXTERN_API_LCD_MAX_WIDTH ||
	    (u32)x + width > lcddev.width || (u32)y + height > lcddev.height)
	{
		return OPERATE_FAILED;
	}
	if (LCD_StreamBegin(x, y, width, height) != 0U)
	{
		return OPERATE_FAILED;
	}

	return ExternAPI_LCD_WritePixels(width, height, pixels);
}

u16 ExternAPI_LCD_GetWidth(void)
{
	return lcddev.width;
}

u16 ExternAPI_LCD_GetHeight(void)
{
	return lcddev.height;
}

u32 ExternAPI_KeyGetState(void)
{
	return KEY_GetState();
}

u32 ExternAPI_KeyGetRawState(void)
{
	return KEY_GetRawState();
}

u8 ExternAPI_AXF_ShouldExit(void)
{
	return (s_exit_requested != 0U) ? 1U : 0U;
}

static u8 ExternAPI_IsCurrentAXF(void)
{
	osThreadId_t current = osThreadGetId();

	return (current != NULL && current == s_axf_thread) ? 1U : 0U;
}

static u8 ExternAPI_IsLCDOwner(void)
{
	osThreadId_t current = osThreadGetId();

	return (current != NULL && current == s_lcd_owner &&
	        current == s_axf_thread && s_lcd_line != NULL) ? 1U : 0U;
}

static int ExternAPI_LCD_WriteColor(u16 width, u16 height, u16 color)
{
	u8 *line = s_lcd_line;
	u16 x;
	u16 y;

	for (x = 0U; x < width; x++)
	{
		ExternAPI_LCD_EncodePixel(&line[(u32)x * 2U], color);
	}
	for (y = 0U; y < height; y++)
	{
		if (LCD_StreamWriteAsync((const u16 *)line, width) != 0U ||
		    LCD_StreamWait() != 0U)
		{
			LCD_StreamEnd();
			return OPERATE_FAILED;
		}
	}

	LCD_StreamEnd();
	return SYSTEM_STATUS_OK;
}

static int ExternAPI_LCD_WritePixels(u16 width, u16 height, const u16 *pixels)
{
	u8 *line = s_lcd_line;
	u32 offset = 0U;
	u16 x;
	u16 y;

	for (y = 0U; y < height; y++)
	{
		for (x = 0U; x < width; x++)
		{
			ExternAPI_LCD_EncodePixel(&line[(u32)x * 2U], pixels[offset + x]);
		}
		offset += width;
		if (LCD_StreamWriteAsync((const u16 *)line, width) != 0U ||
		    LCD_StreamWait() != 0U)
		{
			LCD_StreamEnd();
			return OPERATE_FAILED;
		}
	}

	LCD_StreamEnd();
	return SYSTEM_STATUS_OK;
}

static void ExternAPI_LCD_EncodePixel(u8 *dst, u16 color)
{
	dst[0] = (u8)(color >> 8);
	dst[1] = (u8)color;
}
