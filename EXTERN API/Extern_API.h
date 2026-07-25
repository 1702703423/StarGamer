#ifndef EXTERN_API_H
#define EXTERN_API_H

#include "sys.h"

#ifdef __cplusplus
extern "C" {
#endif

int ExternAPI_AXF_Begin(osThreadId_t tid);
void ExternAPI_AXF_RequestExit(osThreadId_t tid);
int ExternAPI_AXF_End(osThreadId_t tid);
int ExternAPI_AXF_ForceEnd(osThreadId_t tid);

int ExternAPI_LCD_Acquire(void);
int ExternAPI_LCD_Release(void);
int ExternAPI_LCD_Clear(u16 color);
int ExternAPI_LCD_DrawPixel(u16 x, u16 y, u16 color);
int ExternAPI_LCD_FillRect(u16 x, u16 y, u16 width, u16 height, u16 color);
int ExternAPI_LCD_BlitRGB565(u16 x, u16 y, u16 width, u16 height,
	const u16 *pixels);
u16 ExternAPI_LCD_GetWidth(void);
u16 ExternAPI_LCD_GetHeight(void);

u32 ExternAPI_KeyGetState(void);
u32 ExternAPI_KeyGetRawState(void);
u8 ExternAPI_AXF_ShouldExit(void);

#ifdef __cplusplus
}
#endif

#endif
