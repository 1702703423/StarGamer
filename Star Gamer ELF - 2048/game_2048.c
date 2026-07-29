#include "stargamer_axf_api.h"

#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int32_t s32;

#define GAME_FB_WIDTH      320U
#define GAME_FB_HEIGHT     240U
#define GAME_BOARD_SIZE    4U
#define GAME_TILE_SIZE     46
#define GAME_TILE_GAP      3
#define GAME_BOARD_X       60
#define GAME_BOARD_Y       38
#define GAME_BOARD_PIXELS  (GAME_TILE_SIZE * 4 + GAME_TILE_GAP * 5)

#define GAME_RGB565(r, g, b) \
	(u16)((((u16)(r) & 0xF8U) << 8) | \
	       (((u16)(g) & 0xFCU) << 3) | (((u16)(b)) >> 3))

#define GAME_COLOR_BG       GAME_RGB565(14U, 18U, 24U)
#define GAME_COLOR_PANEL    GAME_RGB565(34U, 42U, 53U)
#define GAME_COLOR_BOARD    GAME_RGB565(58U, 68U, 82U)
#define GAME_COLOR_EMPTY    GAME_RGB565(27U, 34U, 45U)
#define GAME_COLOR_TEXT     GAME_RGB565(246U, 249U, 252U)
#define GAME_COLOR_MUTED    GAME_RGB565(172U, 184U, 198U)
#define GAME_COLOR_DARK     GAME_RGB565(26U, 31U, 38U)
#define GAME_COLOR_OVERLAY  GAME_RGB565(20U, 26U, 36U)
#define GAME_COLOR_TITLE    GAME_RGB565(255U, 210U, 79U)

typedef enum Game_Direction {
	GAME_DIR_UP = 0,
	GAME_DIR_LEFT,
	GAME_DIR_RIGHT,
	GAME_DIR_DOWN
} Game_Direction_t;

static u16 s_framebuffer[GAME_FB_WIDTH * GAME_FB_HEIGHT];
static u8 s_board[GAME_BOARD_SIZE][GAME_BOARD_SIZE];
static u32 s_score;
static u32 s_best_score;
static u32 s_rng_state;
static u8 s_game_over;
static u8 s_won;
static u8 s_win_seen;

static const u8 s_font_digits[10][7] =
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

static const u8 s_font_letters[26][7] =
{
	{0x0EU, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U},
	{0x1EU, 0x11U, 0x11U, 0x1EU, 0x11U, 0x11U, 0x1EU},
	{0x0EU, 0x11U, 0x10U, 0x10U, 0x10U, 0x11U, 0x0EU},
	{0x1EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x1EU},
	{0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x1FU},
	{0x1FU, 0x10U, 0x10U, 0x1EU, 0x10U, 0x10U, 0x10U},
	{0x0EU, 0x11U, 0x10U, 0x17U, 0x11U, 0x11U, 0x0FU},
	{0x11U, 0x11U, 0x11U, 0x1FU, 0x11U, 0x11U, 0x11U},
	{0x0EU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x0EU},
	{0x01U, 0x01U, 0x01U, 0x01U, 0x11U, 0x11U, 0x0EU},
	{0x11U, 0x12U, 0x14U, 0x18U, 0x14U, 0x12U, 0x11U},
	{0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x10U, 0x1FU},
	{0x11U, 0x1BU, 0x15U, 0x15U, 0x11U, 0x11U, 0x11U},
	{0x11U, 0x19U, 0x15U, 0x13U, 0x11U, 0x11U, 0x11U},
	{0x0EU, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU},
	{0x1EU, 0x11U, 0x11U, 0x1EU, 0x10U, 0x10U, 0x10U},
	{0x0EU, 0x11U, 0x11U, 0x11U, 0x15U, 0x12U, 0x0DU},
	{0x1EU, 0x11U, 0x11U, 0x1EU, 0x14U, 0x12U, 0x11U},
	{0x0FU, 0x10U, 0x10U, 0x0EU, 0x01U, 0x01U, 0x1EU},
	{0x1FU, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U, 0x04U},
	{0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0EU},
	{0x11U, 0x11U, 0x11U, 0x11U, 0x11U, 0x0AU, 0x04U},
	{0x11U, 0x11U, 0x11U, 0x15U, 0x15U, 0x15U, 0x0AU},
	{0x11U, 0x11U, 0x0AU, 0x04U, 0x0AU, 0x11U, 0x11U},
	{0x11U, 0x11U, 0x0AU, 0x04U, 0x04U, 0x04U, 0x04U},
	{0x1FU, 0x01U, 0x02U, 0x04U, 0x08U, 0x10U, 0x1FU}
};

static const u16 s_tile_colors[] =
{
	GAME_COLOR_EMPTY,
	GAME_RGB565(106U, 215U, 201U),
	GAME_RGB565(247U, 220U, 111U),
	GAME_RGB565(255U, 138U, 101U),
	GAME_RGB565(79U, 195U, 247U),
	GAME_RGB565(129U, 199U, 132U),
	GAME_RGB565(229U, 115U, 115U),
	GAME_RGB565(186U, 104U, 200U),
	GAME_RGB565(255U, 213U, 79U),
	GAME_RGB565(77U, 208U, 225U),
	GAME_RGB565(240U, 98U, 146U),
	GAME_RGB565(250U, 250U, 250U),
	GAME_RGB565(255U, 171U, 64U),
	GAME_RGB565(149U, 117U, 205U),
	GAME_RGB565(76U, 175U, 80U),
	GAME_RGB565(244U, 67U, 54U)
};

static void game_fill(u16 color)
{
	u32 i;

	for (i = 0U; i < GAME_FB_WIDTH * GAME_FB_HEIGHT; i++)
	{
		s_framebuffer[i] = color;
	}
}

static void game_pixel(s32 x, s32 y, u16 color)
{
	if (x < 0 || y < 0 || x >= (s32)GAME_FB_WIDTH ||
	    y >= (s32)GAME_FB_HEIGHT)
	{
		return;
	}
	s_framebuffer[(u32)y * GAME_FB_WIDTH + (u32)x] = color;
}

static void game_rect(s32 x, s32 y, s32 width, s32 height, u16 color)
{
	s32 row;
	s32 col;

	for (row = 0; row < height; row++)
	{
		for (col = 0; col < width; col++)
		{
			game_pixel(x + col, y + row, color);
		}
	}
}

static void game_round_rect(s32 x, s32 y, s32 width, s32 height, s32 radius,
	u16 color)
{
	s32 row;
	s32 col;
	s32 right = width - radius - 1;
	s32 bottom = height - radius - 1;

	for (row = 0; row < height; row++)
	{
		for (col = 0; col < width; col++)
		{
			s32 dx = 0;
			s32 dy = 0;

			if (col < radius)
			{
				dx = radius - col;
			}
			else if (col > right)
			{
				dx = col - right;
			}
			if (row < radius)
			{
				dy = radius - row;
			}
			else if (row > bottom)
			{
				dy = row - bottom;
			}
			if ((dx * dx + dy * dy) <= radius * radius)
			{
				game_pixel(x + col, y + row, color);
			}
		}
	}
}

static const u8 *game_glyph(char character)
{
	static const u8 blank[7] = {0U, 0U, 0U, 0U, 0U, 0U, 0U};

	if (character >= '0' && character <= '9')
	{
		return s_font_digits[(u8)(character - '0')];
	}
	if (character >= 'A' && character <= 'Z')
	{
		return s_font_letters[(u8)(character - 'A')];
	}
	return blank;
}

static void game_char(s32 x, s32 y, char character, u16 color, u8 scale)
{
	const u8 *glyph = game_glyph(character);
	u8 row;
	u8 col;

	for (row = 0U; row < 7U; row++)
	{
		for (col = 0U; col < 5U; col++)
		{
			if ((glyph[row] & (u8)(1U << (4U - col))) != 0U)
			{
				game_rect(x + (s32)col * scale, y + (s32)row * scale,
					scale, scale, color);
			}
		}
	}
}

static s32 game_text_width(const char *text, u8 scale)
{
	s32 width = 0;

	while (*text != '\0')
	{
		if (width != 0)
		{
			width += (s32)scale;
		}
		width += (s32)(5U * scale);
		text++;
	}
	return width;
}

static void game_text(s32 x, s32 y, const char *text, u16 color, u8 scale)
{
	while (*text != '\0')
	{
		game_char(x, y, *text, color, scale);
		x += (s32)(6U * scale);
		text++;
	}
}

static void game_text_center(s32 cx, s32 y, const char *text, u16 color,
	u8 scale)
{
	game_text(cx - game_text_width(text, scale) / 2, y, text, color, scale);
}

static u8 game_digit_count(u32 value)
{
	u8 count = 1U;

	while (value >= 10U)
	{
		value /= 10U;
		count++;
	}
	return count;
}

static s32 game_number_width(u32 value, u8 scale)
{
	u8 digits = game_digit_count(value);

	return (s32)((digits - 1U) * 6U * scale + 5U * scale);
}

static void game_number(s32 x, s32 y, u32 value, u16 color, u8 scale)
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
		game_char(x, y, digits[count], color, scale);
		x += (s32)(6U * scale);
	}
}

static void game_number_right(s32 right, s32 y, u32 value, u16 color, u8 scale)
{
	game_number(right - game_number_width(value, scale), y, value, color, scale);
}

static u32 game_tile_value(u8 exponent)
{
	if (exponent >= 31U)
	{
		return 0x80000000UL;
	}
	return 1UL << exponent;
}

static u32 game_rand(void)
{
	u32 value = s_rng_state;

	if (value == 0U)
	{
		value = 0x2048A11DU;
	}
	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	s_rng_state = value;
	return value;
}

static void game_random_mix(u32 value)
{
	s_rng_state ^= value + 0x9E3779B9UL + (s_rng_state << 6) +
		(s_rng_state >> 2);
	(void)game_rand();
}

static u16 game_tile_color(u8 exponent)
{
	u8 index = exponent;

	if (index >= (u8)(sizeof(s_tile_colors) / sizeof(s_tile_colors[0])))
	{
		index = (u8)((sizeof(s_tile_colors) / sizeof(s_tile_colors[0])) - 1U);
	}
	return s_tile_colors[index];
}

static u16 game_tile_text_color(u8 exponent)
{
	if (exponent <= 5U || exponent == 8U || exponent == 9U ||
	    exponent == 11U)
	{
		return GAME_COLOR_DARK;
	}
	return GAME_COLOR_TEXT;
}

static void game_clear_board(void)
{
	u8 y;
	u8 x;

	for (y = 0U; y < GAME_BOARD_SIZE; y++)
	{
		for (x = 0U; x < GAME_BOARD_SIZE; x++)
		{
			s_board[y][x] = 0U;
		}
	}
}

static u8 game_empty_count(void)
{
	u8 y;
	u8 x;
	u8 count = 0U;

	for (y = 0U; y < GAME_BOARD_SIZE; y++)
	{
		for (x = 0U; x < GAME_BOARD_SIZE; x++)
		{
			if (s_board[y][x] == 0U)
			{
				count++;
			}
		}
	}
	return count;
}

static void game_add_random_tile(void)
{
	u8 empty = game_empty_count();
	u8 target;
	u8 y;
	u8 x;

	if (empty == 0U)
	{
		return;
	}
	target = (u8)(game_rand() % empty);

	for (y = 0U; y < GAME_BOARD_SIZE; y++)
	{
		for (x = 0U; x < GAME_BOARD_SIZE; x++)
		{
			if (s_board[y][x] == 0U)
			{
				if (target == 0U)
				{
					s_board[y][x] = ((game_rand() % 10U) == 0U) ? 2U : 1U;
					return;
				}
				target--;
			}
		}
	}
}

static u8 game_can_move(void)
{
	u8 y;
	u8 x;

	if (game_empty_count() != 0U)
	{
		return 1U;
	}
	for (y = 0U; y < GAME_BOARD_SIZE; y++)
	{
		for (x = 0U; x < GAME_BOARD_SIZE; x++)
		{
			if (x + 1U < GAME_BOARD_SIZE &&
			    s_board[y][x] == s_board[y][x + 1U])
			{
				return 1U;
			}
			if (y + 1U < GAME_BOARD_SIZE &&
			    s_board[y][x] == s_board[y + 1U][x])
			{
				return 1U;
			}
		}
	}
	return 0U;
}

static u8 game_read_line(u8 line, u8 pos, Game_Direction_t direction)
{
	switch (direction)
	{
	case GAME_DIR_UP:
		return s_board[pos][line];
	case GAME_DIR_LEFT:
		return s_board[line][pos];
	case GAME_DIR_RIGHT:
		return s_board[line][GAME_BOARD_SIZE - 1U - pos];
	case GAME_DIR_DOWN:
	default:
		return s_board[GAME_BOARD_SIZE - 1U - pos][line];
	}
}

static void game_write_line(u8 line, u8 pos, Game_Direction_t direction,
	u8 value)
{
	switch (direction)
	{
	case GAME_DIR_UP:
		s_board[pos][line] = value;
		break;
	case GAME_DIR_LEFT:
		s_board[line][pos] = value;
		break;
	case GAME_DIR_RIGHT:
		s_board[line][GAME_BOARD_SIZE - 1U - pos] = value;
		break;
	case GAME_DIR_DOWN:
	default:
		s_board[GAME_BOARD_SIZE - 1U - pos][line] = value;
		break;
	}
}

static u8 game_move_line(u8 line, Game_Direction_t direction)
{
	u8 old_line[GAME_BOARD_SIZE];
	u8 packed[GAME_BOARD_SIZE];
	u8 output[GAME_BOARD_SIZE];
	u8 packed_count = 0U;
	u8 output_count = 0U;
	u8 changed = 0U;
	u8 pos;
	u8 i;

	for (pos = 0U; pos < GAME_BOARD_SIZE; pos++)
	{
		u8 value = game_read_line(line, pos, direction);

		old_line[pos] = value;
		output[pos] = 0U;
		if (value != 0U)
		{
			packed[packed_count++] = value;
		}
	}

	i = 0U;
	while (i < packed_count)
	{
		u8 value = packed[i];

		if (i + 1U < packed_count && packed[i + 1U] == value)
		{
			value++;
			s_score += game_tile_value(value);
			if (s_score > s_best_score)
			{
				s_best_score = s_score;
			}
			if (value >= 11U && s_win_seen == 0U)
			{
				s_won = 1U;
				s_win_seen = 1U;
			}
			i = (u8)(i + 2U);
		}
		else
		{
			i++;
		}
		output[output_count++] = value;
	}

	for (pos = 0U; pos < GAME_BOARD_SIZE; pos++)
	{
		if (output[pos] != old_line[pos])
		{
			changed = 1U;
		}
		game_write_line(line, pos, direction, output[pos]);
	}
	return changed;
}

static u8 game_move(Game_Direction_t direction)
{
	u8 line;
	u8 changed = 0U;

	for (line = 0U; line < GAME_BOARD_SIZE; line++)
	{
		if (game_move_line(line, direction) != 0U)
		{
			changed = 1U;
		}
	}

	if (changed != 0U)
	{
		game_add_random_tile();
		s_game_over = (game_can_move() == 0U) ? 1U : 0U;
	}
	return changed;
}

static void game_new(void)
{
	game_clear_board();
	s_score = 0U;
	s_game_over = 0U;
	s_won = 0U;
	s_win_seen = 0U;
	game_add_random_tile();
	game_add_random_tile();
}

static void game_draw_score_box(s32 x, s32 y, const char *label, u32 value)
{
	game_round_rect(x, y, 88, 30, 4, GAME_COLOR_PANEL);
	game_text(x + 8, y + 5, label, GAME_COLOR_MUTED, 1U);
	game_number_right(x + 80, y + 17, value, GAME_COLOR_TEXT, 1U);
}

static void game_draw_tile(s32 x, s32 y, u8 exponent)
{
	u16 color = game_tile_color(exponent);

	game_round_rect(x, y, GAME_TILE_SIZE, GAME_TILE_SIZE, 5, color);
	if (exponent != 0U)
	{
		u32 value = game_tile_value(exponent);
		u8 digits = game_digit_count(value);
		u8 scale = (digits <= 2U) ? 3U : 2U;
		s32 text_width;
		s32 text_x;
		s32 text_y;

		if (digits >= 5U)
		{
			scale = 1U;
		}
		text_width = game_number_width(value, scale);
		text_x = x + GAME_TILE_SIZE / 2 - text_width / 2;
		text_y = y + GAME_TILE_SIZE / 2 - (s32)(7U * scale) / 2;
		game_number(text_x, text_y, value, game_tile_text_color(exponent),
			scale);
	}
}

static void game_render(void)
{
	u8 y;
	u8 x;

	game_fill(GAME_COLOR_BG);
	game_text(10, 6, "2048", GAME_COLOR_TITLE, 3U);
	game_draw_score_box(104, 4, "SCORE", s_score);
	game_draw_score_box(222, 4, "BEST", s_best_score);

	game_round_rect(GAME_BOARD_X, GAME_BOARD_Y, GAME_BOARD_PIXELS,
		GAME_BOARD_PIXELS, 6, GAME_COLOR_BOARD);
	for (y = 0U; y < GAME_BOARD_SIZE; y++)
	{
		for (x = 0U; x < GAME_BOARD_SIZE; x++)
		{
			s32 tile_x = GAME_BOARD_X + GAME_TILE_GAP +
				(s32)x * (GAME_TILE_SIZE + GAME_TILE_GAP);
			s32 tile_y = GAME_BOARD_Y + GAME_TILE_GAP +
				(s32)y * (GAME_TILE_SIZE + GAME_TILE_GAP);

			game_draw_tile(tile_x, tile_y, s_board[y][x]);
		}
	}

	if (s_won != 0U)
	{
		game_round_rect(47, 92, 226, 58, 6, GAME_COLOR_OVERLAY);
		game_text_center(160, 106, "YOU WIN", GAME_COLOR_TITLE, 3U);
	}
	else if (s_game_over != 0U)
	{
		game_round_rect(47, 92, 226, 58, 6, GAME_COLOR_OVERLAY);
		game_text_center(160, 104, "GAME OVER", GAME_COLOR_TEXT, 3U);
	}
}

static int game_present(void)
{
	return SG_LCD_BlitRGB565(0U, 0U, GAME_FB_WIDTH, GAME_FB_HEIGHT,
		s_framebuffer);
}

static u8 game_handle_key_press(u32 pressed)
{
	u8 dirty = 0U;

	if (s_won != 0U)
	{
		if ((pressed & (SG_KEY_OK | SG_KEY_A | SG_KEY_B)) != 0U)
		{
			s_won = 0U;
			dirty = 1U;
		}
		return dirty;
	}

	if (s_game_over != 0U)
	{
		if ((pressed & (SG_KEY_OK | SG_KEY_A | SG_KEY_B)) != 0U)
		{
			game_new();
			dirty = 1U;
		}
		return dirty;
	}

	if ((pressed & SG_KEY_UP) != 0U)
	{
		dirty = game_move(GAME_DIR_UP);
	}
	else if ((pressed & SG_KEY_LEFT) != 0U)
	{
		dirty = game_move(GAME_DIR_LEFT);
	}
	else if ((pressed & SG_KEY_RIGHT) != 0U)
	{
		dirty = game_move(GAME_DIR_RIGHT);
	}
	else if ((pressed & SG_KEY_DOWN) != 0U)
	{
		dirty = game_move(GAME_DIR_DOWN);
	}
	return dirty;
}

int dl_main(int argc, char *argv[])
{
	u32 previous_keys;

	(void)argc;
	(void)argv;

	s_rng_state = 0x2048A11DUL ^ (u32)(uintptr_t)&s_framebuffer[0] ^
		SG_KeyGetRawState();
	game_new();
	previous_keys = SG_KeyGetState();

	if (SG_LCD_Acquire() != SG_API_OK)
	{
		return -1;
	}

	game_render();
	if (game_present() != SG_API_OK)
	{
		(void)SG_LCD_Release();
		return -2;
	}

	while (SG_ShouldExit() == 0U)
	{
		u32 keys = SG_KeyGetState();
		u32 pressed = keys & ~previous_keys;

		game_random_mix(keys);
		if (game_handle_key_press(pressed) != 0U)
		{
			game_render();
			if (game_present() != SG_API_OK)
			{
				break;
			}
		}
		previous_keys = keys;
		SG_AXF_Delay(10U);
	}

	(void)SG_LCD_Release();
	return 0;
}
