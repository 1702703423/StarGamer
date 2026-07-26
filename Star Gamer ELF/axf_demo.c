#include "stargamer_axf_api.h"

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;

#define AXF_FB_WIDTH  320U
#define AXF_FB_HEIGHT 240U
#define AXF_FONT_SCALE 2U

#define AXF_RGB565(r, g, b) \
	(u16)((((u16)(r) & 0x1FU) << 11) | \
	       (((u16)(g) & 0x3FU) << 5) | ((u16)(b) & 0x1FU))

#define AXF_COLOR_BG       AXF_RGB565(1U, 3U, 8U)
#define AXF_COLOR_PANEL    AXF_RGB565(3U, 8U, 16U)
#define AXF_COLOR_TEXT     AXF_RGB565(31U, 63U, 63U)
#define AXF_COLOR_TRIANGLE AXF_RGB565(31U, 8U, 4U)
#define AXF_COLOR_SQUARE   AXF_RGB565(3U, 45U, 23U)
#define AXF_COLOR_STAR     AXF_RGB565(31U, 48U, 2U)

static u16 s_framebuffer[AXF_FB_WIDTH * AXF_FB_HEIGHT];
static u32 s_counters[4];

static void axf_fill(u16 color)
{
	u32 i;

	for (i = 0U; i < AXF_FB_WIDTH * AXF_FB_HEIGHT; i++)
	{
		s_framebuffer[i] = color;
	}
}

static void axf_pixel(s32 x, s32 y, u16 color)
{
	if (x < 0 || y < 0 || x >= (s32)AXF_FB_WIDTH ||
		y >= (s32)AXF_FB_HEIGHT)
	{
		return;
	}
	s_framebuffer[(u32)y * AXF_FB_WIDTH + (u32)x] = color;
}

static void axf_rect(s32 x, s32 y, s32 width, s32 height, u16 color)
{
	s32 row;
	s32 col;

	for (row = 0; row < height; row++)
	{
		for (col = 0; col < width; col++)
		{
			axf_pixel(x + col, y + row, color);
		}
	}
}

static s32 axf_abs(s32 value)
{
	return (value < 0) ? -value : value;
}

static void axf_line(s32 x0, s32 y0, s32 x1, s32 y1, u16 color)
{
	s32 dx = axf_abs(x1 - x0);
	s32 sx = (x0 < x1) ? 1 : -1;
	s32 dy = -axf_abs(y1 - y0);
	s32 sy = (y0 < y1) ? 1 : -1;
	s32 error = dx + dy;

	while (1)
	{
		s32 twice_error;

		axf_pixel(x0, y0, color);
		if (x0 == x1 && y0 == y1)
		{
			break;
		}
		twice_error = error * 2;
		if (twice_error >= dy)
		{
			error += dy;
			x0 += sx;
		}
		if (twice_error <= dx)
		{
			error += dx;
			y0 += sy;
		}
	}
}

static void axf_fill_polygon(const s32 *x, const s32 *y, u8 count,
	u16 color)
{
	s32 intersections[10];
	s32 min_y = y[0];
	s32 max_y = y[0];
	s32 scan_y;
	u8 i;

	for (i = 1U; i < count; i++)
	{
		if (y[i] < min_y)
		{
			min_y = y[i];
		}
		if (y[i] > max_y)
		{
			max_y = y[i];
		}
	}

	for (scan_y = min_y; scan_y <= max_y; scan_y++)
	{
		u8 hit_count = 0U;
		u8 j;

		for (i = 0U; i < count; i++)
		{
			u8 next = (u8)((i + 1U) % count);
			if (((y[i] <= scan_y) && (y[next] > scan_y)) ||
			    ((y[next] <= scan_y) && (y[i] > scan_y)))
			{
				intersections[hit_count++] = x[i] +
					((scan_y - y[i]) * (x[next] - x[i])) /
					(y[next] - y[i]);
			}
		}

		for (i = 0U; i + 1U < hit_count; i++)
		{
			for (j = (u8)(i + 1U); j < hit_count; j++)
			{
				if (intersections[j] < intersections[i])
				{
					s32 temp = intersections[i];
					intersections[i] = intersections[j];
					intersections[j] = temp;
				}
			}
		}

		for (i = 0U; i + 1U < hit_count; i = (u8)(i + 2U))
		{
			s32 start = intersections[i];
			s32 end = intersections[i + 1U];
			s32 scan_x;

			for (scan_x = start; scan_x <= end; scan_x++)
			{
				axf_pixel(scan_x, scan_y, color);
			}
		}
	}
}

static const u8 *axf_glyph(char character)
{
	static const u8 blank[7] = {0U, 0U, 0U, 0U, 0U, 0U, 0U};
	static const u8 digit[10][7] =
	{
		{0x0EU, 0x11U, 0x13U, 0x15U, 0x19U, 0x11U, 0x0EU},
		{0x04U, 0x0CU, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU},
		{0x0EU, 0x11U, 0x01U, 0x06U, 0x08U, 0x10U, 0x1FU},
		{0x1EU, 0x01U, 0x01U, 0x0EU, 0x01U, 0x01U, 0x1EU},
		{0x02U, 0x06U, 0x0AU, 0x12U, 0x1FU, 0x02U, 0x02U},
		{0x1FU, 0x10U, 0x1EU, 0x01U, 0x01U, 0x11U, 0x0EU},
		{0x06U, 0x08U, 0x10U, 0x1EU, 0x11U, 0x11U, 0x0EU},
		{0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x08U, 0x08U},
		{0x0EU, 0x11U, 0x11U, 0x0EU, 0x11U, 0x11U, 0x0EU},
		{0x0EU, 0x11U, 0x11U, 0x0FU, 0x01U, 0x02U, 0x0CU}
	};
	static const u8 letter_a[7] = {0x0EU, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U};
	static const u8 letter_b[7] = {0x1EU, 0x11U, 0x11U, 0x1EU, 0x11U, 0x11U, 0x1EU};
	static const u8 letter_c[7] = {0x0EU, 0x11U, 0x10U, 0x10U, 0x10U, 0x11U, 0x0EU};
	static const u8 letter_p[7] = {0x1EU, 0x11U, 0x11U, 0x1EU, 0x10U, 0x10U, 0x10U};

	if (character >= '0' && character <= '9')
	{
		return digit[(u8)(character - '0')];
	}
	if (character == 'A')
	{
		return letter_a;
	}
	if (character == 'B')
	{
		return letter_b;
	}
	if (character == 'C')
	{
		return letter_c;
	}
	if (character == 'P')
	{
		return letter_p;
	}
	return blank;
}

static void axf_char(s32 x, s32 y, char character, u16 color, u8 scale)
{
	const u8 *glyph = axf_glyph(character);
	u8 row;
	u8 col;

	for (row = 0U; row < 7U; row++)
	{
		for (col = 0U; col < 5U; col++)
		{
			if ((glyph[row] & (u8)(1U << (4U - col))) != 0U)
			{
				axf_rect(x + (s32)col * scale, y + (s32)row * scale,
				         scale, scale, color);
			}
		}
	}
}

static void axf_text(s32 x, s32 y, const char *text, u16 color, u8 scale)
{
	while (*text != '\0')
	{
		axf_char(x, y, *text, color, scale);
		x += (s32)(6U * scale);
		text++;
	}
}

static void axf_number(s32 x, s32 y, u32 value, u16 color)
{
	char digits[10];
	u8 count = 0U;

	if (value == 0U)
	{
		digits[count++] = '0';
	}
	else
	{
		while (value != 0U && count < (u8)sizeof(digits))
		{
			digits[count++] = (char)('0' + (value % 10U));
			value /= 10U;
		}
	}

	while (count != 0U)
	{
		count--;
		axf_char(x, y, digits[count], color, 1U);
		x += 6;
	}
}

static void axf_render(void)
{
	static const s32 triangle_x[3] = {60, 18, 102};
	static const s32 triangle_y[3] = {24, 110, 110};
	static const s32 star_x[10] =
		{266, 276, 311, 282, 295, 266, 237, 250, 221, 256};
	static const s32 star_y[10] =
		{22, 52, 52, 73, 112, 88, 112, 73, 52, 52};
	u8 i;

	axf_fill(AXF_COLOR_BG);
	axf_fill_polygon(triangle_x, triangle_y, 3U, AXF_COLOR_TRIANGLE);
	axf_rect(124, 28, 78, 82, AXF_COLOR_SQUARE);
	axf_rect(132, 36, 62, 66, AXF_COLOR_BG);
	/* A thicker outline makes the five-point star readable at a glance. */
	for (i = 0U; i < 10U; i++)
	{
		u8 next = (u8)((i + 1U) % 10U);
		axf_line(star_x[i], star_y[i], star_x[next], star_y[next], AXF_COLOR_STAR);
		axf_line(star_x[i] + 1, star_y[i], star_x[next] + 1,
		         star_y[next], AXF_COLOR_STAR);
	}

	for (i = 0U; i < 4U; i++)
	{
		s32 x = 10 + (s32)i * 80;
		axf_rect(x, 138, 70, 76, AXF_COLOR_PANEL);
	}
	axf_text(20, 146, "PB1", AXF_COLOR_TEXT, AXF_FONT_SCALE);
	axf_text(100, 146, "PA2", AXF_COLOR_TEXT, AXF_FONT_SCALE);
	axf_text(180, 146, "PC1", AXF_COLOR_TEXT, AXF_FONT_SCALE);
	axf_text(260, 146, "PA1", AXF_COLOR_TEXT, AXF_FONT_SCALE);
	for (i = 0U; i < 4U; i++)
	{
		axf_number(18 + (s32)i * 80, 176, s_counters[i], AXF_COLOR_TEXT);
	}
}

static int axf_present(void)
{
	return SG_LCD_BlitRGB565(0U, 0U, AXF_FB_WIDTH, AXF_FB_HEIGHT,
	                         s_framebuffer);
}

int dl_main(int argc, char *argv[])
{
	u32 previous_keys;

	(void)argc;
	(void)argv;
	s_counters[0] = 0U;
	s_counters[1] = 0U;
	s_counters[2] = 0U;
	s_counters[3] = 0U;
	previous_keys = SG_KeyGetState();

	if (SG_LCD_Acquire() != SG_API_OK)
	{
		return -1;
	}

	axf_render();
	if (axf_present() != SG_API_OK)
	{
		(void)SG_LCD_Release();
		return -2;
	}

	while (SG_ShouldExit() == 0U)
	{
		u32 keys = SG_KeyGetState();
		u8 dirty = 0U;

		if ((keys & SG_KEY_UP) != 0U && (previous_keys & SG_KEY_UP) == 0U)
		{
			s_counters[0]++;
			dirty = 1U;
		}
		if ((keys & SG_KEY_RIGHT) != 0U &&
		    (previous_keys & SG_KEY_RIGHT) == 0U)
		{
			s_counters[1]++;
			dirty = 1U;
		}
		if ((keys & SG_KEY_LEFT) != 0U &&
		    (previous_keys & SG_KEY_LEFT) == 0U)
		{
			s_counters[2]++;
			dirty = 1U;
		}
		if ((keys & SG_KEY_DOWN) != 0U &&
		    (previous_keys & SG_KEY_DOWN) == 0U)
		{
			s_counters[3]++;
			dirty = 1U;
		}
		previous_keys = keys;

		if (dirty != 0U)
		{
			axf_render();
			if (axf_present() != SG_API_OK)
			{
				break;
			}
		}
		SG_AXF_Delay(10U);
	}

	(void)SG_LCD_Release();
	return 0;
}
