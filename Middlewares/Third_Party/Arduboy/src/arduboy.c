#include "arduboy.h"
#include "glcdfont.h"
#include "ab_logo.h"

#include <string.h>

static u8 s_buffer[ARDUBOY_BUFFER_SIZE];
static s16 s_cursor_x;
static s16 s_cursor_y;
static u8 s_text_size;
static u8 s_wrap;

static u8 s_frame_rate;
static u16 s_frame_count;
static u8 s_frame_millis;
static u32 s_last_frame_start;
static u32 s_next_frame_start;
static u8 s_post_render;
static u8 s_last_frame_duration_ms;

static u8 Arduboy_Pressed(u8 buttons);
static u8 Arduboy_NotPressed(u8 buttons);
static void Arduboy_Begin(void);
static void Arduboy_BeginNoLogo(void);
static void Arduboy_BootLogo(void);
static void Arduboy_Clear(void);
static void Arduboy_Display(void);
static void Arduboy_DrawPixel(s16 x, s16 y, u8 color);
static u8 Arduboy_GetPixel(s16 x, s16 y);
static void Arduboy_DrawCircle(s16 x0, s16 y0, u8 radius, u8 color);
static void Arduboy_DrawCircleHelper(s16 x0, s16 y0, u8 radius, u8 corners, u8 color);
static void Arduboy_FillCircle(s16 x0, s16 y0, u8 radius, u8 color);
static void Arduboy_FillCircleHelper(s16 x0, s16 y0, u8 radius, u8 corners, s16 delta, u8 color);
static void Arduboy_DrawLine(s16 x0, s16 y0, s16 x1, s16 y1, u8 color);
static void Arduboy_DrawRect(s16 x, s16 y, u8 w, u8 h, u8 color);
static void Arduboy_DrawFastVLine(s16 x, s16 y, u8 h, u8 color);
static void Arduboy_DrawFastHLine(s16 x, s16 y, u8 w, u8 color);
static void Arduboy_FillRect(s16 x, s16 y, u8 w, u8 h, u8 color);
static void Arduboy_FillScreen(u8 color);
static void Arduboy_DrawRoundRect(s16 x, s16 y, u8 w, u8 h, u8 radius, u8 color);
static void Arduboy_FillRoundRect(s16 x, s16 y, u8 w, u8 h, u8 radius, u8 color);
static void Arduboy_DrawTriangle(s16 x0, s16 y0, s16 x1, s16 y1, s16 x2, s16 y2, u8 color);
static void Arduboy_FillTriangle(s16 x0, s16 y0, s16 x1, s16 y1, s16 x2, s16 y2, u8 color);
static void Arduboy_DrawBitmap(s16 x, s16 y, const u8 *bitmap, u8 w, u8 h, u8 color);
static void Arduboy_DrawSlowXYBitmap(s16 x, s16 y, const u8 *bitmap, u8 w, u8 h, u8 color);
static void Arduboy_DrawChar(s16 x, s16 y, u8 character, u8 color, u8 background, u8 size);
static void Arduboy_SetCursor(s16 x, s16 y);
static void Arduboy_SetTextSize(u8 size);
static void Arduboy_SetTextWrap(u8 enabled);
static u8 *Arduboy_GetBuffer(void);
static u32 Arduboy_Write(u8 character);
static void Arduboy_Print(const char *text);
static void Arduboy_InitRandomSeed(void);
static void Arduboy_Swap(s16 *a, s16 *b);
static void Arduboy_SetFrameRate(u8 rate);
static u8 Arduboy_NextFrame(void);
static u8 Arduboy_EveryXFrames(u8 frames);
static u8 Arduboy_CpuLoad(void);

static s16 Arduboy_Abs(s16 value)
{
	return (value < 0) ? (s16)-value : value;
}

static u8 Arduboy_Pressed(u8 buttons)
{
	return ((buttonsState() & buttons) == buttons) ? 1U : 0U;
}

static u8 Arduboy_NotPressed(u8 buttons)
{
	return ((buttonsState() & buttons) == 0U) ? 1U : 0U;
}

static void Arduboy_Begin(void)
{
	Arduboy_SetFrameRate(60U);
	s_frame_count = 0U;
	s_last_frame_start = ArduboyCore_Millis();
	s_next_frame_start = s_last_frame_start;
	s_post_render = 0U;
	s_last_frame_duration_ms = 0U;
	s_cursor_x = 0;
	s_cursor_y = 0;
	s_text_size = 1U;
	s_wrap = 1U;
	Arduboy_Clear();
}

static void Arduboy_BeginNoLogo(void)
{
	Arduboy_Begin();
}

static void Arduboy_BootLogo(void)
{
	s16 y;

	for (y = -18; y <= 24; y++)
	{
		if (ArduboyCore_ShouldStop() != 0U)
		{
			return;
		}

		Arduboy_Clear();
		Arduboy_DrawBitmap(20, y, arduboy_logo, 88U, 16U, WHITE);
		Arduboy_Display();
		ArduboyCore_Delay(27U);
		if (y == -16)
		{
			ArduboyCore_Delay(250U);
		}
	}

	ArduboyCore_Delay(750U);
}

static void Arduboy_Clear(void)
{
	Arduboy_FillScreen(BLACK);
}

static void Arduboy_Display(void)
{
	paintScreen(s_buffer);
}

static void Arduboy_DrawPixel(s16 x, s16 y, u8 color)
{
	u32 index;
	u8 mask;

	if (x < 0 || x >= (s16)ARDUBOY_WIDTH || y < 0 || y >= (s16)ARDUBOY_HEIGHT)
	{
		return;
	}

	index = ((u32)y / 8U) * ARDUBOY_WIDTH + (u32)x;
	mask = (u8)(1U << ((u32)y & 7U));

	if (color == WHITE)
	{
		s_buffer[index] |= mask;
	}
	else if (color == BLACK)
	{
		s_buffer[index] &= (u8)~mask;
	}
	else if (color == INVERT)
	{
		s_buffer[index] ^= mask;
	}
}

static u8 Arduboy_GetPixel(s16 x, s16 y)
{
	u32 index;
	u8 mask;

	if (x < 0 || x >= (s16)ARDUBOY_WIDTH || y < 0 || y >= (s16)ARDUBOY_HEIGHT)
	{
		return 0U;
	}

	index = ((u32)y / 8U) * ARDUBOY_WIDTH + (u32)x;
	mask = (u8)(1U << ((u32)y & 7U));
	return ((s_buffer[index] & mask) != 0U) ? 1U : 0U;
}

static void Arduboy_DrawCircle(s16 x0, s16 y0, u8 radius, u8 color)
{
	s16 f = (s16)(1 - (s16)radius);
	s16 ddx = 1;
	s16 ddy = (s16)(-2 * (s16)radius);
	s16 x = 0;
	s16 y = (s16)radius;

	Arduboy_DrawPixel(x0, (s16)(y0 + radius), color);
	Arduboy_DrawPixel(x0, (s16)(y0 - radius), color);
	Arduboy_DrawPixel((s16)(x0 + radius), y0, color);
	Arduboy_DrawPixel((s16)(x0 - radius), y0, color);

	while (x < y)
	{
		if (f >= 0)
		{
			y--;
			ddy = (s16)(ddy + 2);
			f = (s16)(f + ddy);
		}
		x++;
		ddx = (s16)(ddx + 2);
		f = (s16)(f + ddx);

		Arduboy_DrawPixel((s16)(x0 + x), (s16)(y0 + y), color);
		Arduboy_DrawPixel((s16)(x0 - x), (s16)(y0 + y), color);
		Arduboy_DrawPixel((s16)(x0 + x), (s16)(y0 - y), color);
		Arduboy_DrawPixel((s16)(x0 - x), (s16)(y0 - y), color);
		Arduboy_DrawPixel((s16)(x0 + y), (s16)(y0 + x), color);
		Arduboy_DrawPixel((s16)(x0 - y), (s16)(y0 + x), color);
		Arduboy_DrawPixel((s16)(x0 + y), (s16)(y0 - x), color);
		Arduboy_DrawPixel((s16)(x0 - y), (s16)(y0 - x), color);
	}
}

static void Arduboy_DrawCircleHelper(s16 x0, s16 y0, u8 radius, u8 corners, u8 color)
{
	s16 f = (s16)(1 - (s16)radius);
	s16 ddx = 1;
	s16 ddy = (s16)(-2 * (s16)radius);
	s16 x = 0;
	s16 y = (s16)radius;

	while (x < y)
	{
		if (f >= 0)
		{
			y--;
			ddy = (s16)(ddy + 2);
			f = (s16)(f + ddy);
		}
		x++;
		ddx = (s16)(ddx + 2);
		f = (s16)(f + ddx);

		if ((corners & 0x4U) != 0U)
		{
			Arduboy_DrawPixel((s16)(x0 + x), (s16)(y0 + y), color);
			Arduboy_DrawPixel((s16)(x0 + y), (s16)(y0 + x), color);
		}
		if ((corners & 0x2U) != 0U)
		{
			Arduboy_DrawPixel((s16)(x0 + x), (s16)(y0 - y), color);
			Arduboy_DrawPixel((s16)(x0 + y), (s16)(y0 - x), color);
		}
		if ((corners & 0x8U) != 0U)
		{
			Arduboy_DrawPixel((s16)(x0 - y), (s16)(y0 + x), color);
			Arduboy_DrawPixel((s16)(x0 - x), (s16)(y0 + y), color);
		}
		if ((corners & 0x1U) != 0U)
		{
			Arduboy_DrawPixel((s16)(x0 - y), (s16)(y0 - x), color);
			Arduboy_DrawPixel((s16)(x0 - x), (s16)(y0 - y), color);
		}
	}
}

static void Arduboy_FillCircle(s16 x0, s16 y0, u8 radius, u8 color)
{
	Arduboy_DrawFastVLine(x0, (s16)(y0 - radius), (u8)(2U * radius + 1U), color);
	Arduboy_FillCircleHelper(x0, y0, radius, 3U, 0, color);
}

static void Arduboy_FillCircleHelper(s16 x0, s16 y0, u8 radius, u8 corners, s16 delta, u8 color)
{
	s16 f = (s16)(1 - (s16)radius);
	s16 ddx = 1;
	s16 ddy = (s16)(-2 * (s16)radius);
	s16 x = 0;
	s16 y = (s16)radius;
	s16 line_height;

	while (x < y)
	{
		if (f >= 0)
		{
			y--;
			ddy = (s16)(ddy + 2);
			f = (s16)(f + ddy);
		}
		x++;
		ddx = (s16)(ddx + 2);
		f = (s16)(f + ddx);

		if ((corners & 0x1U) != 0U)
		{
			line_height = (s16)(2 * y + 1 + delta);
			if (line_height > 0)
			{
				Arduboy_DrawFastVLine((s16)(x0 + x), (s16)(y0 - y), (u8)line_height, color);
			}
			line_height = (s16)(2 * x + 1 + delta);
			if (line_height > 0)
			{
				Arduboy_DrawFastVLine((s16)(x0 + y), (s16)(y0 - x), (u8)line_height, color);
			}
		}
		if ((corners & 0x2U) != 0U)
		{
			line_height = (s16)(2 * y + 1 + delta);
			if (line_height > 0)
			{
				Arduboy_DrawFastVLine((s16)(x0 - x), (s16)(y0 - y), (u8)line_height, color);
			}
			line_height = (s16)(2 * x + 1 + delta);
			if (line_height > 0)
			{
				Arduboy_DrawFastVLine((s16)(x0 - y), (s16)(y0 - x), (u8)line_height, color);
			}
		}
	}
}

static void Arduboy_Swap(s16 *a, s16 *b)
{
	s16 value;

	if (a == NULL || b == NULL)
	{
		return;
	}

	value = *a;
	*a = *b;
	*b = value;
}

static void Arduboy_DrawLine(s16 x0, s16 y0, s16 x1, s16 y1, u8 color)
{
	u8 steep = (Arduboy_Abs((s16)(y1 - y0)) > Arduboy_Abs((s16)(x1 - x0))) ? 1U : 0U;
	s16 dx;
	s16 dy;
	s16 y_step;
	s32 error;

	if (steep != 0U)
	{
		Arduboy_Swap(&x0, &y0);
		Arduboy_Swap(&x1, &y1);
	}
	if (x0 > x1)
	{
		Arduboy_Swap(&x0, &x1);
		Arduboy_Swap(&y0, &y1);
	}

	dx = (s16)(x1 - x0);
	dy = Arduboy_Abs((s16)(y1 - y0));
	y_step = (y0 < y1) ? 1 : -1;
	error = (s32)dx / 2;

	for (; x0 <= x1; x0++)
	{
		if (steep != 0U)
		{
			Arduboy_DrawPixel(y0, x0, color);
		}
		else
		{
			Arduboy_DrawPixel(x0, y0, color);
		}

		error -= dy;
		if (error < 0)
		{
			y0 = (s16)(y0 + y_step);
			error += dx;
		}
	}
}

static void Arduboy_DrawFastVLine(s16 x, s16 y, u8 h, u8 color)
{
	s16 start;
	s16 end;
	s16 row;

	if (x < 0 || x >= (s16)ARDUBOY_WIDTH || h == 0U)
	{
		return;
	}

	start = (y < 0) ? 0 : y;
	end = (s16)(y + (s16)h);
	if (end > (s16)ARDUBOY_HEIGHT)
	{
		end = (s16)ARDUBOY_HEIGHT;
	}

	for (row = start; row < end; row++)
	{
		Arduboy_DrawPixel(x, row, color);
	}
}

static void Arduboy_DrawFastHLine(s16 x, s16 y, u8 w, u8 color)
{
	s16 start;
	s16 end;
	s16 column;

	if (y < 0 || y >= (s16)ARDUBOY_HEIGHT || w == 0U)
	{
		return;
	}

	start = (x < 0) ? 0 : x;
	end = (s16)(x + (s16)w);
	if (end > (s16)ARDUBOY_WIDTH)
	{
		end = (s16)ARDUBOY_WIDTH;
	}

	for (column = start; column < end; column++)
	{
		Arduboy_DrawPixel(column, y, color);
	}
}

static void Arduboy_DrawRect(s16 x, s16 y, u8 w, u8 h, u8 color)
{
	if (w == 0U || h == 0U)
	{
		return;
	}

	Arduboy_DrawFastHLine(x, y, w, color);
	Arduboy_DrawFastHLine(x, (s16)(y + h - 1U), w, color);
	Arduboy_DrawFastVLine(x, y, h, color);
	Arduboy_DrawFastVLine((s16)(x + w - 1U), y, h, color);
}

static void Arduboy_FillRect(s16 x, s16 y, u8 w, u8 h, u8 color)
{
	s16 column;
	s16 end = (s16)(x + (s16)w);

	for (column = x; column < end; column++)
	{
		Arduboy_DrawFastVLine(column, y, h, color);
	}
}

static void Arduboy_FillScreen(u8 color)
{
	u32 i;

	if (color == INVERT)
	{
		for (i = 0U; i < ARDUBOY_BUFFER_SIZE; i++)
		{
			s_buffer[i] = (u8)~s_buffer[i];
		}
	}
	else
	{
		memset(s_buffer, (color == BLACK) ? 0x00 : 0xFF, sizeof(s_buffer));
	}
}

static u8 Arduboy_ClampRadius(u8 w, u8 h, u8 radius)
{
	u8 maximum = (w < h) ? (u8)(w / 2U) : (u8)(h / 2U);
	return (radius > maximum) ? maximum : radius;
}

static void Arduboy_DrawRoundRect(s16 x, s16 y, u8 w, u8 h, u8 radius, u8 color)
{
	radius = Arduboy_ClampRadius(w, h, radius);
	if (w == 0U || h == 0U)
	{
		return;
	}

	Arduboy_DrawFastHLine((s16)(x + radius), y, (u8)(w - 2U * radius), color);
	Arduboy_DrawFastHLine((s16)(x + radius), (s16)(y + h - 1U), (u8)(w - 2U * radius), color);
	Arduboy_DrawFastVLine(x, (s16)(y + radius), (u8)(h - 2U * radius), color);
	Arduboy_DrawFastVLine((s16)(x + w - 1U), (s16)(y + radius), (u8)(h - 2U * radius), color);
	Arduboy_DrawCircleHelper((s16)(x + radius), (s16)(y + radius), radius, 1U, color);
	Arduboy_DrawCircleHelper((s16)(x + w - radius - 1U), (s16)(y + radius), radius, 2U, color);
	Arduboy_DrawCircleHelper((s16)(x + w - radius - 1U), (s16)(y + h - radius - 1U), radius, 4U, color);
	Arduboy_DrawCircleHelper((s16)(x + radius), (s16)(y + h - radius - 1U), radius, 8U, color);
}

static void Arduboy_FillRoundRect(s16 x, s16 y, u8 w, u8 h, u8 radius, u8 color)
{
	radius = Arduboy_ClampRadius(w, h, radius);
	if (w == 0U || h == 0U)
	{
		return;
	}

	Arduboy_FillRect((s16)(x + radius), y, (u8)(w - 2U * radius), h, color);
	Arduboy_FillCircleHelper((s16)(x + w - radius - 1U), (s16)(y + radius), radius, 1U,
	                         (s16)(h - 2U * radius - 1U), color);
	Arduboy_FillCircleHelper((s16)(x + radius), (s16)(y + radius), radius, 2U,
	                         (s16)(h - 2U * radius - 1U), color);
}

static void Arduboy_DrawTriangle(s16 x0, s16 y0, s16 x1, s16 y1, s16 x2, s16 y2, u8 color)
{
	Arduboy_DrawLine(x0, y0, x1, y1, color);
	Arduboy_DrawLine(x1, y1, x2, y2, color);
	Arduboy_DrawLine(x2, y2, x0, y0, color);
}

static void Arduboy_FillTriangle(s16 x0, s16 y0, s16 x1, s16 y1, s16 x2, s16 y2, u8 color)
{
	s16 a;
	s16 b;
	s16 y;
	s16 last;
	s16 dx01;
	s16 dy01;
	s16 dx02;
	s16 dy02;
	s16 dx12;
	s16 dy12;
	s32 sa = 0;
	s32 sb = 0;

	if (y0 > y1)
	{
		Arduboy_Swap(&y0, &y1);
		Arduboy_Swap(&x0, &x1);
	}
	if (y1 > y2)
	{
		Arduboy_Swap(&y1, &y2);
		Arduboy_Swap(&x1, &x2);
	}
	if (y0 > y1)
	{
		Arduboy_Swap(&y0, &y1);
		Arduboy_Swap(&x0, &x1);
	}

	if (y0 == y2)
	{
		a = x0;
		b = x0;
		if (x1 < a) a = x1;
		if (x1 > b) b = x1;
		if (x2 < a) a = x2;
		if (x2 > b) b = x2;
		Arduboy_DrawFastHLine(a, y0, (u8)(b - a + 1), color);
		return;
	}

	dx01 = (s16)(x1 - x0);
	dy01 = (s16)(y1 - y0);
	dx02 = (s16)(x2 - x0);
	dy02 = (s16)(y2 - y0);
	dx12 = (s16)(x2 - x1);
	dy12 = (s16)(y2 - y1);
	last = (y1 == y2) ? y1 : (s16)(y1 - 1);

	for (y = y0; y <= last; y++)
	{
		a = (dy01 != 0) ? (s16)(x0 + sa / dy01) : x0;
		b = (s16)(x0 + sb / dy02);
		sa += dx01;
		sb += dx02;
		if (a > b)
		{
			Arduboy_Swap(&a, &b);
		}
		Arduboy_DrawFastHLine(a, y, (u8)(b - a + 1), color);
	}

	sa = (s32)dx12 * (y - y1);
	sb = (s32)dx02 * (y - y0);
	for (; y <= y2; y++)
	{
		a = (dy12 != 0) ? (s16)(x1 + sa / dy12) : x1;
		b = (s16)(x0 + sb / dy02);
		sa += dx12;
		sb += dx02;
		if (a > b)
		{
			Arduboy_Swap(&a, &b);
		}
		Arduboy_DrawFastHLine(a, y, (u8)(b - a + 1), color);
	}
}

static void Arduboy_DrawBitmap(s16 x, s16 y, const u8 *bitmap, u8 w, u8 h, u8 color)
{
	u16 source_x;
	u16 source_y;
	u32 index;

	if (bitmap == NULL || w == 0U || h == 0U ||
	    (s32)x + w <= 0 || x >= (s16)ARDUBOY_WIDTH ||
	    (s32)y + h <= 0 || y >= (s16)ARDUBOY_HEIGHT)
	{
		return;
	}

	for (source_y = 0U; source_y < h; source_y++)
	{
		for (source_x = 0U; source_x < w; source_x++)
		{
			index = ((u32)source_y / 8U) * w + source_x;
			if ((bitmap[index] & (u8)(1U << (source_y & 7U))) != 0U)
			{
				Arduboy_DrawPixel((s16)(x + source_x), (s16)(y + source_y), color);
			}
		}
	}
}

static void Arduboy_DrawSlowXYBitmap(s16 x, s16 y, const u8 *bitmap, u8 w, u8 h, u8 color)
{
	u16 source_x;
	u16 source_y;
	u16 byte_width;

	if (bitmap == NULL || w == 0U || h == 0U ||
	    (s32)x + w <= 0 || x >= (s16)ARDUBOY_WIDTH ||
	    (s32)y + h <= 0 || y >= (s16)ARDUBOY_HEIGHT)
	{
		return;
	}

	byte_width = (u16)((w + 7U) / 8U);
	for (source_y = 0U; source_y < h; source_y++)
	{
		for (source_x = 0U; source_x < w; source_x++)
		{
			if ((bitmap[source_y * byte_width + source_x / 8U] & (u8)(0x80U >> (source_x & 7U))) != 0U)
			{
				Arduboy_DrawPixel((s16)(x + source_x), (s16)(y + source_y), color);
			}
		}
	}
}

static void Arduboy_DrawChar(s16 x, s16 y, u8 character, u8 color, u8 background, u8 size)
{
	u8 column;
	u8 row;
	u8 scale_x;
	u8 scale_y;
	u8 line;
	u8 draw_color;
	u8 draw_background = (background != color) ? 1U : 0U;

	if (size == 0U || x >= (s16)ARDUBOY_WIDTH || y >= (s16)ARDUBOY_HEIGHT ||
	    (s32)x + 6 * size <= 0 || (s32)y + 8 * size <= 0)
	{
		return;
	}

	for (column = 0U; column < 6U; column++)
	{
		line = (column == 5U) ? 0U : font[(u32)character * 5U + column];
		for (row = 0U; row < 8U; row++)
		{
			draw_color = ((line & 1U) != 0U) ? color : background;
			if (draw_color != BLACK || draw_background != 0U)
			{
				for (scale_x = 0U; scale_x < size; scale_x++)
				{
					for (scale_y = 0U; scale_y < size; scale_y++)
					{
						Arduboy_DrawPixel((s16)(x + column * size + scale_x),
						                  (s16)(y + row * size + scale_y), draw_color);
					}
				}
			}
			line >>= 1U;
		}
	}
}

static void Arduboy_SetCursor(s16 x, s16 y)
{
	s_cursor_x = x;
	s_cursor_y = y;
}

static void Arduboy_SetTextSize(u8 size)
{
	s_text_size = (size == 0U) ? 1U : size;
}

static void Arduboy_SetTextWrap(u8 enabled)
{
	s_wrap = (enabled != 0U) ? 1U : 0U;
}

static u8 *Arduboy_GetBuffer(void)
{
	return s_buffer;
}

static u32 Arduboy_Write(u8 character)
{
	if (character == (u8)'\n')
	{
		s_cursor_y = (s16)(s_cursor_y + s_text_size * 8U);
		s_cursor_x = 0;
	}
	else if (character != (u8)'\r')
	{
		Arduboy_DrawChar(s_cursor_x, s_cursor_y, character, WHITE, BLACK, s_text_size);
		s_cursor_x = (s16)(s_cursor_x + s_text_size * 6U);
		if (s_wrap != 0U && s_cursor_x > (s16)(ARDUBOY_WIDTH - s_text_size * 6U))
		{
			(void)Arduboy_Write((u8)'\n');
		}
	}

	return 1U;
}

static void Arduboy_Print(const char *text)
{
	if (text == NULL)
	{
		return;
	}

	while (*text != '\0')
	{
		(void)Arduboy_Write((u8)*text);
		text++;
	}
}

static void Arduboy_InitRandomSeed(void)
{
	/* The platform RNG is already initialized and does not require reseeding. */
}

static void Arduboy_SetFrameRate(u8 rate)
{
	if (rate == 0U)
	{
		rate = 1U;
	}

	s_frame_rate = rate;
	s_frame_millis = (u8)(1000U / rate);
	if (s_frame_millis == 0U)
	{
		s_frame_millis = 1U;
	}
}

static u8 Arduboy_NextFrame(void)
{
	u32 now = ArduboyCore_Millis();
	u32 duration;

	if (s_post_render != 0U)
	{
		duration = now - s_last_frame_start;
		s_last_frame_duration_ms = (duration > 255U) ? 255U : (u8)duration;
		s_frame_count++;
		s_post_render = 0U;
	}

	if ((s32)(now - s_next_frame_start) < 0)
	{
		if (ArduboyCore_ShouldStop() == 0U)
		{
			ArduboyCore_Delay(1U);
		}
		return 0U;
	}

	s_next_frame_start = now + s_frame_millis;
	s_last_frame_start = now;
	s_post_render = 1U;
	return 1U;
}

static u8 Arduboy_EveryXFrames(u8 frames)
{
	return (frames != 0U && (s_frame_count % frames) == 0U) ? 1U : 0U;
}

static u8 Arduboy_CpuLoad(void)
{
	u32 load;

	if (s_frame_millis == 0U)
	{
		return 0U;
	}

	load = (u32)s_last_frame_duration_ms * 100U / s_frame_millis;
	return (load > 255U) ? 255U : (u8)load;
}

u8 Arduboy_SelfTest(void)
{
	static const u8 clipped_bitmap[2] = {0xFFU, 0xFFU};
	u32 i;

	Arduboy_Clear();
	Arduboy_DrawPixel(-1, -1, WHITE);
	Arduboy_DrawPixel((s16)ARDUBOY_WIDTH, (s16)ARDUBOY_HEIGHT, WHITE);
	for (i = 0U; i < ARDUBOY_BUFFER_SIZE; i++)
	{
		if (s_buffer[i] != 0U)
		{
			return 0U;
		}
	}

	Arduboy_DrawPixel(0, 0, WHITE);
	if (Arduboy_GetPixel(0, 0) == 0U)
	{
		return 0U;
	}
	Arduboy_DrawPixel(0, 0, INVERT);
	if (Arduboy_GetPixel(0, 0) != 0U)
	{
		return 0U;
	}

	Arduboy_DrawBitmap(-1, -1, clipped_bitmap, 2U, 8U, WHITE);
	if (Arduboy_GetPixel(0, 0) == 0U || Arduboy_GetPixel(0, 6) == 0U || Arduboy_GetPixel(0, 7) != 0U)
	{
		return 0U;
	}

	Arduboy_Clear();
	return 1U;
}

Arduboy arduboy =
{
	.width = width,
	.height = height,
	.pressed = Arduboy_Pressed,
	.notPressed = Arduboy_NotPressed,
	.begin = Arduboy_Begin,
	.beginNoLogo = Arduboy_BeginNoLogo,
	.bootLogo = Arduboy_BootLogo,
	.clear = Arduboy_Clear,
	.display = Arduboy_Display,
	.drawPixel = Arduboy_DrawPixel,
	.getPixel = Arduboy_GetPixel,
	.drawCircle = Arduboy_DrawCircle,
	.drawCircleHelper = Arduboy_DrawCircleHelper,
	.fillCircle = Arduboy_FillCircle,
	.fillCircleHelper = Arduboy_FillCircleHelper,
	.drawLine = Arduboy_DrawLine,
	.drawRect = Arduboy_DrawRect,
	.drawFastVLine = Arduboy_DrawFastVLine,
	.drawFastHLine = Arduboy_DrawFastHLine,
	.fillRect = Arduboy_FillRect,
	.fillScreen = Arduboy_FillScreen,
	.drawRoundRect = Arduboy_DrawRoundRect,
	.fillRoundRect = Arduboy_FillRoundRect,
	.drawTriangle = Arduboy_DrawTriangle,
	.fillTriangle = Arduboy_FillTriangle,
	.drawBitmap = Arduboy_DrawBitmap,
	.drawSlowXYBitmap = Arduboy_DrawSlowXYBitmap,
	.drawChar = Arduboy_DrawChar,
	.setCursor = Arduboy_SetCursor,
	.setTextSize = Arduboy_SetTextSize,
	.setTextWrap = Arduboy_SetTextWrap,
	.getBuffer = Arduboy_GetBuffer,
	.write = Arduboy_Write,
	.print = Arduboy_Print,
	.initRandomSeed = Arduboy_InitRandomSeed,
	.swap = Arduboy_Swap,
	.setFrameRate = Arduboy_SetFrameRate,
	.nextFrame = Arduboy_NextFrame,
	.everyXFrames = Arduboy_EveryXFrames,
	.cpuLoad = Arduboy_CpuLoad,
};
