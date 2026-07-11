#ifndef ARDUBOY_H
#define ARDUBOY_H

#include "arduboy_core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIDTH   ARDUBOY_WIDTH
#define HEIGHT  ARDUBOY_HEIGHT
#define BLACK   ARDUBOY_BLACK
#define WHITE   ARDUBOY_WHITE
#define INVERT  ARDUBOY_INVERT

typedef struct ArduboyStruct
{
	u8 (*width)(void);
	u8 (*height)(void);
	u8 (*pressed)(u8 buttons);
	u8 (*notPressed)(u8 buttons);
	void (*begin)(void);
	void (*beginNoLogo)(void);
	void (*bootLogo)(void);
	void (*clear)(void);
	void (*display)(void);
	void (*drawPixel)(s16 x, s16 y, u8 color);
	u8 (*getPixel)(s16 x, s16 y);
	void (*drawCircle)(s16 x0, s16 y0, u8 radius, u8 color);
	void (*drawCircleHelper)(s16 x0, s16 y0, u8 radius, u8 corners, u8 color);
	void (*fillCircle)(s16 x0, s16 y0, u8 radius, u8 color);
	void (*fillCircleHelper)(s16 x0, s16 y0, u8 radius, u8 corners, s16 delta, u8 color);
	void (*drawLine)(s16 x0, s16 y0, s16 x1, s16 y1, u8 color);
	void (*drawRect)(s16 x, s16 y, u8 w, u8 h, u8 color);
	void (*drawFastVLine)(s16 x, s16 y, u8 h, u8 color);
	void (*drawFastHLine)(s16 x, s16 y, u8 w, u8 color);
	void (*fillRect)(s16 x, s16 y, u8 w, u8 h, u8 color);
	void (*fillScreen)(u8 color);
	void (*drawRoundRect)(s16 x, s16 y, u8 w, u8 h, u8 radius, u8 color);
	void (*fillRoundRect)(s16 x, s16 y, u8 w, u8 h, u8 radius, u8 color);
	void (*drawTriangle)(s16 x0, s16 y0, s16 x1, s16 y1, s16 x2, s16 y2, u8 color);
	void (*fillTriangle)(s16 x0, s16 y0, s16 x1, s16 y1, s16 x2, s16 y2, u8 color);
	void (*drawBitmap)(s16 x, s16 y, const u8 *bitmap, u8 w, u8 h, u8 color);
	void (*drawSlowXYBitmap)(s16 x, s16 y, const u8 *bitmap, u8 w, u8 h, u8 color);
	void (*drawChar)(s16 x, s16 y, u8 character, u8 color, u8 background, u8 size);
	void (*setCursor)(s16 x, s16 y);
	void (*setTextSize)(u8 size);
	void (*setTextWrap)(u8 enabled);
	u8 *(*getBuffer)(void);
	u32 (*write)(u8 character);
	void (*print)(const char *text);
	void (*initRandomSeed)(void);
	void (*swap)(s16 *a, s16 *b);
	void (*setFrameRate)(u8 rate);
	u8 (*nextFrame)(void);
	u8 (*everyXFrames)(u8 frames);
	u8 (*cpuLoad)(void);
} Arduboy;

extern Arduboy arduboy;

u8 Arduboy_SelfTest(void);

#define delay(milliseconds) ArduboyCore_Delay((u32)(milliseconds))
#define millis()            ArduboyCore_Millis()
#define random(minimum, maximum) \
	ArduboyCore_Random((s32)(minimum), (s32)(maximum))

#ifdef __cplusplus
}
#endif

#endif
