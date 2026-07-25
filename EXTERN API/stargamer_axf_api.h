#ifndef STARGAMER_AXF_API_H
#define STARGAMER_AXF_API_H

#include <stdint.h>
#include "dl_extern_lib.h"
#include "dl_user_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SG_API_OK            0

#define SG_KEY_UP            (1UL << 0)
#define SG_KEY_LEFT          (1UL << 1)
#define SG_KEY_RIGHT         (1UL << 2)
#define SG_KEY_DOWN          (1UL << 3)
#define SG_KEY_OK            (1UL << 4)
#define SG_KEY_A             (1UL << 5)
#define SG_KEY_EXIT          (1UL << 6)
#define SG_KEY_B             (1UL << 7)

static inline int SG_LCD_Acquire(void)
{
	DL_DECLARE_EXTERN_LIB_FUNC(SG_LCD_Acquire, DL_USER_LIB_BASE,
		DL_USER_LIB_NUM, DL_USER_LCD_ACQUIRE, int, void);
	return DL_FUNC_NAME(SG_LCD_Acquire)();
}

static inline int SG_LCD_Release(void)
{
	DL_DECLARE_EXTERN_LIB_FUNC(SG_LCD_Release, DL_USER_LIB_BASE,
		DL_USER_LIB_NUM, DL_USER_LCD_RELEASE, int, void);
	return DL_FUNC_NAME(SG_LCD_Release)();
}

static inline int SG_LCD_Clear(uint16_t color)
{
	DL_DECLARE_EXTERN_LIB_FUNC(SG_LCD_Clear, DL_USER_LIB_BASE,
		DL_USER_LIB_NUM, DL_USER_LCD_CLEAR, int, uint16_t);
	return DL_FUNC_NAME(SG_LCD_Clear)(color);
}

static inline int SG_LCD_DrawPixel(uint16_t x, uint16_t y, uint16_t color)
{
	DL_DECLARE_EXTERN_LIB_FUNC(SG_LCD_DrawPixel, DL_USER_LIB_BASE,
		DL_USER_LIB_NUM, DL_USER_LCD_DRAW_PIXEL, int,
		uint16_t, uint16_t, uint16_t);
	return DL_FUNC_NAME(SG_LCD_DrawPixel)(x, y, color);
}

static inline int SG_LCD_FillRect(uint16_t x, uint16_t y, uint16_t width,
	uint16_t height, uint16_t color)
{
	DL_DECLARE_EXTERN_LIB_FUNC(SG_LCD_FillRect, DL_USER_LIB_BASE,
		DL_USER_LIB_NUM, DL_USER_LCD_FILL_RECT, int,
		uint16_t, uint16_t, uint16_t, uint16_t, uint16_t);
	return DL_FUNC_NAME(SG_LCD_FillRect)(x, y, width, height, color);
}

static inline int SG_LCD_BlitRGB565(uint16_t x, uint16_t y, uint16_t width,
	uint16_t height, const uint16_t *pixels)
{
	DL_DECLARE_EXTERN_LIB_FUNC(SG_LCD_BlitRGB565, DL_USER_LIB_BASE,
		DL_USER_LIB_NUM, DL_USER_LCD_BLIT_RGB565, int,
		uint16_t, uint16_t, uint16_t, uint16_t, const uint16_t *);
	return DL_FUNC_NAME(SG_LCD_BlitRGB565)(x, y, width, height, pixels);
}

static inline uint16_t SG_LCD_GetWidth(void)
{
	DL_DECLARE_EXTERN_LIB_FUNC(SG_LCD_GetWidth, DL_USER_LIB_BASE,
		DL_USER_LIB_NUM, DL_USER_LCD_GET_WIDTH, uint16_t, void);
	return DL_FUNC_NAME(SG_LCD_GetWidth)();
}

static inline uint16_t SG_LCD_GetHeight(void)
{
	DL_DECLARE_EXTERN_LIB_FUNC(SG_LCD_GetHeight, DL_USER_LIB_BASE,
		DL_USER_LIB_NUM, DL_USER_LCD_GET_HEIGHT, uint16_t, void);
	return DL_FUNC_NAME(SG_LCD_GetHeight)();
}

static inline uint32_t SG_KeyGetState(void)
{
	DL_DECLARE_EXTERN_LIB_FUNC(SG_KeyGetState, DL_USER_LIB_BASE,
		DL_USER_LIB_NUM, DL_USER_KEY_GET_STATE, uint32_t, void);
	return DL_FUNC_NAME(SG_KeyGetState)();
}

static inline uint32_t SG_KeyGetRawState(void)
{
	DL_DECLARE_EXTERN_LIB_FUNC(SG_KeyGetRawState, DL_USER_LIB_BASE,
		DL_USER_LIB_NUM, DL_USER_KEY_GET_RAW_STATE, uint32_t, void);
	return DL_FUNC_NAME(SG_KeyGetRawState)();
}

static inline uint8_t SG_ShouldExit(void)
{
	DL_DECLARE_EXTERN_LIB_FUNC(SG_ShouldExit, DL_USER_LIB_BASE,
		DL_USER_LIB_NUM, DL_USER_SHOULD_EXIT, uint8_t, void);
	return DL_FUNC_NAME(SG_ShouldExit)();
}

#ifdef __cplusplus
}
#endif

#endif
