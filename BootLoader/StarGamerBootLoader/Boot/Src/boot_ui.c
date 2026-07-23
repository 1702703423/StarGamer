#include "boot_ui.h"

#define BOOT_LCD_WIDTH                  320U
#define BOOT_LCD_HEIGHT                 240U
#define BOOT_LCD_FONT_WIDTH             5U
#define BOOT_LCD_FONT_HEIGHT            7U
#define BOOT_LCD_FONT_STEP              6U

#define BOOT_COLOR_BLACK                0x0000U
#define BOOT_COLOR_WHITE                0xFFFFU
#define BOOT_COLOR_GRAY                 0x8410U
#define BOOT_COLOR_ACCENT               0xFD20U
#define BOOT_COLOR_GREEN                0x07E0U
#define BOOT_COLOR_RED                  0xF800U
#define BOOT_COLOR_PANEL                0x10A2U

#define BOOT_LCD_CS_HIGH()              HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_SET)
#define BOOT_LCD_CS_LOW()               HAL_GPIO_WritePin(GPIOC, GPIO_PIN_4, GPIO_PIN_RESET)
#define BOOT_LCD_DC_HIGH()              HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_SET)
#define BOOT_LCD_DC_LOW()               HAL_GPIO_WritePin(GPIOC, GPIO_PIN_5, GPIO_PIN_RESET)
#define BOOT_LCD_RST_HIGH()             HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_SET)
#define BOOT_LCD_RST_LOW()              HAL_GPIO_WritePin(GPIOB, GPIO_PIN_12, GPIO_PIN_RESET)

typedef struct Boot_Font5x7
{
	char ch;
	u8 col[BOOT_LCD_FONT_WIDTH];
} Boot_Font5x7_t;

static volatile u32 s_boot_tick_ms;

static const Boot_Font5x7_t s_font[] =
{
	{' ', {0x00, 0x00, 0x00, 0x00, 0x00}},
	{'0', {0x3E, 0x51, 0x49, 0x45, 0x3E}},
	{'1', {0x00, 0x42, 0x7F, 0x40, 0x00}},
	{'2', {0x42, 0x61, 0x51, 0x49, 0x46}},
	{'3', {0x21, 0x41, 0x45, 0x4B, 0x31}},
	{'4', {0x18, 0x14, 0x12, 0x7F, 0x10}},
	{'5', {0x27, 0x45, 0x45, 0x45, 0x39}},
	{'6', {0x3C, 0x4A, 0x49, 0x49, 0x30}},
	{'7', {0x01, 0x71, 0x09, 0x05, 0x03}},
	{'8', {0x36, 0x49, 0x49, 0x49, 0x36}},
	{'9', {0x06, 0x49, 0x49, 0x29, 0x1E}},
	{'A', {0x7E, 0x11, 0x11, 0x11, 0x7E}},
	{'B', {0x7F, 0x49, 0x49, 0x49, 0x36}},
	{'C', {0x3E, 0x41, 0x41, 0x41, 0x22}},
	{'D', {0x7F, 0x41, 0x41, 0x22, 0x1C}},
	{'E', {0x7F, 0x49, 0x49, 0x49, 0x41}},
	{'F', {0x7F, 0x09, 0x09, 0x09, 0x01}},
	{'G', {0x3E, 0x41, 0x49, 0x49, 0x7A}},
	{'H', {0x7F, 0x08, 0x08, 0x08, 0x7F}},
	{'I', {0x00, 0x41, 0x7F, 0x41, 0x00}},
	{'J', {0x20, 0x40, 0x41, 0x3F, 0x01}},
	{'K', {0x7F, 0x08, 0x14, 0x22, 0x41}},
	{'L', {0x7F, 0x40, 0x40, 0x40, 0x40}},
	{'M', {0x7F, 0x02, 0x0C, 0x02, 0x7F}},
	{'N', {0x7F, 0x04, 0x08, 0x10, 0x7F}},
	{'O', {0x3E, 0x41, 0x41, 0x41, 0x3E}},
	{'P', {0x7F, 0x09, 0x09, 0x09, 0x06}},
	{'Q', {0x3E, 0x41, 0x51, 0x21, 0x5E}},
	{'R', {0x7F, 0x09, 0x19, 0x29, 0x46}},
	{'S', {0x46, 0x49, 0x49, 0x49, 0x31}},
	{'T', {0x01, 0x01, 0x7F, 0x01, 0x01}},
	{'U', {0x3F, 0x40, 0x40, 0x40, 0x3F}},
	{'V', {0x1F, 0x20, 0x40, 0x20, 0x1F}},
	{'W', {0x7F, 0x20, 0x18, 0x20, 0x7F}},
	{'X', {0x63, 0x14, 0x08, 0x14, 0x63}},
	{'Y', {0x07, 0x08, 0x70, 0x08, 0x07}},
	{'Z', {0x61, 0x51, 0x49, 0x45, 0x43}},
	{':', {0x00, 0x36, 0x36, 0x00, 0x00}},
	{'-', {0x08, 0x08, 0x08, 0x08, 0x08}},
	{'.', {0x00, 0x60, 0x60, 0x00, 0x00}},
	{'/', {0x20, 0x10, 0x08, 0x04, 0x02}},
	{'>', {0x00, 0x41, 0x22, 0x14, 0x08}},
	{'%', {0x63, 0x13, 0x08, 0x64, 0x63}},
	{'?', {0x02, 0x01, 0x51, 0x09, 0x06}}
};

static void Boot_LcdCommand(u8 command);
static void Boot_LcdData8(u8 data);
static void Boot_LcdSetWindow(u16 x0, u16 y0, u16 x1, u16 y1);
static void Boot_LcdFillRect(u16 x, u16 y, u16 width, u16 height, u16 color);
static const u8 *Boot_FontGet(char ch);
static void Boot_LcdChar(u16 x, u16 y, char ch, u16 fg, u16 bg, u8 scale);
static void Boot_LcdText(u16 x, u16 y, const char *text, u16 fg, u16 bg, u8 scale);
static void Boot_LcdWriteColor(u16 color);
static void Boot_FormatPercent(char *text, u8 percent);

void Boot_TickInterrupt(void)
{
	s_boot_tick_ms++;
}

u32 Boot_TickMs(void)
{
	return s_boot_tick_ms;
}

void Boot_DelayMs(u32 delay_ms)
{
	u32 start = Boot_TickMs();

	while ((Boot_TickMs() - start) < delay_ms)
	{
		__WFI();
	}
}

static void Boot_LcdCommand(u8 command)
{
	BOOT_LCD_CS_LOW();
	BOOT_LCD_DC_LOW();
	(void)HAL_SPI_Transmit(&hspi1, &command, 1U, 100U);
	BOOT_LCD_CS_HIGH();
}

static void Boot_LcdData8(u8 data)
{
	BOOT_LCD_CS_LOW();
	BOOT_LCD_DC_HIGH();
	(void)HAL_SPI_Transmit(&hspi1, &data, 1U, 100U);
	BOOT_LCD_CS_HIGH();
}

static void Boot_LcdSetWindow(u16 x0, u16 y0, u16 x1, u16 y1)
{
	Boot_LcdCommand(0x2AU);
	Boot_LcdData8((u8)(x0 >> 8U));
	Boot_LcdData8((u8)x0);
	Boot_LcdData8((u8)(x1 >> 8U));
	Boot_LcdData8((u8)x1);
	Boot_LcdCommand(0x2BU);
	Boot_LcdData8((u8)(y0 >> 8U));
	Boot_LcdData8((u8)y0);
	Boot_LcdData8((u8)(y1 >> 8U));
	Boot_LcdData8((u8)y1);
	Boot_LcdCommand(0x2CU);
}

static void Boot_LcdWriteColor(u16 color)
{
	u8 data[2];
	data[0] = (u8)(color >> 8U);
	data[1] = (u8)color;
	(void)HAL_SPI_Transmit(&hspi1, data, 2U, 100U);
}

static void Boot_LcdFillRect(u16 x, u16 y, u16 width, u16 height, u16 color)
{
	u32 pixels;
	u8 color_data[128];
	u16 i;

	if ((width == 0U) || (height == 0U) || (x >= BOOT_LCD_WIDTH) || (y >= BOOT_LCD_HEIGHT))
	{
		return;
	}
	if (((u32)x + width) > BOOT_LCD_WIDTH)
	{
		width = (u16)(BOOT_LCD_WIDTH - x);
	}
	if (((u32)y + height) > BOOT_LCD_HEIGHT)
	{
		height = (u16)(BOOT_LCD_HEIGHT - y);
	}

	Boot_LcdSetWindow(x, y, (u16)(x + width - 1U), (u16)(y + height - 1U));
	for (i = 0U; i < (sizeof(color_data) / 2U); i++)
	{
		color_data[i * 2U] = (u8)(color >> 8U);
		color_data[i * 2U + 1U] = (u8)color;
	}
	BOOT_LCD_CS_LOW();
	BOOT_LCD_DC_HIGH();
	pixels = (u32)width * height;
	while (pixels > 0U)
	{
		u16 chunk = (pixels > (sizeof(color_data) / 2U)) ?
		            (u16)(sizeof(color_data) / 2U) : (u16)pixels;
		(void)HAL_SPI_Transmit(&hspi1, color_data, (u16)(chunk * 2U), 100U);
		pixels -= chunk;
	}
	BOOT_LCD_CS_HIGH();
}

static const u8 *Boot_FontGet(char ch)
{
	u32 i;

	if ((ch >= 'a') && (ch <= 'z'))
	{
		ch = (char)(ch - ('a' - 'A'));
	}
	for (i = 0U; i < (sizeof(s_font) / sizeof(s_font[0])); i++)
	{
		if (s_font[i].ch == ch)
		{
			return s_font[i].col;
		}
	}
	return s_font[(sizeof(s_font) / sizeof(s_font[0])) - 1U].col;
}

static void Boot_LcdChar(u16 x, u16 y, char ch, u16 fg, u16 bg, u8 scale)
{
	const u8 *glyph = Boot_FontGet(ch);
	u8 col;
	u8 row;
	u8 sx;
	u8 sy;

	Boot_LcdSetWindow(x, y, (u16)(x + BOOT_LCD_FONT_WIDTH * scale - 1U),
		(u16)(y + BOOT_LCD_FONT_HEIGHT * scale - 1U));
	BOOT_LCD_CS_LOW();
	BOOT_LCD_DC_HIGH();
	for (row = 0U; row < BOOT_LCD_FONT_HEIGHT; row++)
	{
		for (sy = 0U; sy < scale; sy++)
		{
			for (col = 0U; col < BOOT_LCD_FONT_WIDTH; col++)
			{
				for (sx = 0U; sx < scale; sx++)
				{
					Boot_LcdWriteColor(((glyph[col] & (1U << row)) != 0U) ? fg : bg);
				}
			}
		}
	}
	BOOT_LCD_CS_HIGH();
}

static void Boot_LcdText(u16 x, u16 y, const char *text, u16 fg, u16 bg, u8 scale)
{
	while ((*text != '\0') && ((u32)x + BOOT_LCD_FONT_WIDTH * scale <= BOOT_LCD_WIDTH))
	{
		Boot_LcdChar(x, y, *text, fg, bg, scale);
		x = (u16)(x + BOOT_LCD_FONT_STEP * scale);
		text++;
	}
}

void Boot_UI_Init(void)
{
	BOOT_LCD_CS_HIGH();
	BOOT_LCD_DC_HIGH();
	HAL_GPIO_WritePin(GPIOB, GPIO_PIN_15, GPIO_PIN_SET);
	BOOT_LCD_RST_LOW();
	Boot_DelayMs(20U);
	BOOT_LCD_RST_HIGH();
	Boot_DelayMs(120U);

	Boot_LcdCommand(0x01U);
	Boot_DelayMs(120U);
	Boot_LcdCommand(0x11U);
	Boot_DelayMs(120U);
	Boot_LcdCommand(0x36U);
	Boot_LcdData8(0x60U);
	Boot_LcdCommand(0x3AU);
	Boot_LcdData8(0x05U);
	Boot_LcdCommand(0xB2U);
	Boot_LcdData8(0x0CU);
	Boot_LcdData8(0x0CU);
	Boot_LcdData8(0x00U);
	Boot_LcdData8(0x33U);
	Boot_LcdData8(0x33U);
	Boot_LcdCommand(0xB7U);
	Boot_LcdData8(0x35U);
	Boot_LcdCommand(0xBBU);
	Boot_LcdData8(0x19U);
	Boot_LcdCommand(0xC0U);
	Boot_LcdData8(0x2CU);
	Boot_LcdCommand(0xC2U);
	Boot_LcdData8(0x01U);
	Boot_LcdCommand(0xC3U);
	Boot_LcdData8(0x12U);
	Boot_LcdCommand(0xC4U);
	Boot_LcdData8(0x20U);
	Boot_LcdCommand(0xC6U);
	Boot_LcdData8(0x0FU);
	Boot_LcdCommand(0xD0U);
	Boot_LcdData8(0xA4U);
	Boot_LcdData8(0xA1U);
	Boot_LcdCommand(0xE0U);
	Boot_LcdData8(0xD0U);
	Boot_LcdData8(0x04U);
	Boot_LcdData8(0x0DU);
	Boot_LcdData8(0x11U);
	Boot_LcdData8(0x13U);
	Boot_LcdData8(0x2BU);
	Boot_LcdData8(0x3FU);
	Boot_LcdData8(0x54U);
	Boot_LcdData8(0x4CU);
	Boot_LcdData8(0x18U);
	Boot_LcdData8(0x0DU);
	Boot_LcdData8(0x0BU);
	Boot_LcdData8(0x1FU);
	Boot_LcdData8(0x23U);
	Boot_LcdCommand(0xE1U);
	Boot_LcdData8(0xD0U);
	Boot_LcdData8(0x04U);
	Boot_LcdData8(0x0CU);
	Boot_LcdData8(0x11U);
	Boot_LcdData8(0x13U);
	Boot_LcdData8(0x2CU);
	Boot_LcdData8(0x3FU);
	Boot_LcdData8(0x44U);
	Boot_LcdData8(0x51U);
	Boot_LcdData8(0x2FU);
	Boot_LcdData8(0x1FU);
	Boot_LcdData8(0x1FU);
	Boot_LcdData8(0x20U);
	Boot_LcdData8(0x23U);
	Boot_LcdCommand(0x21U);
	Boot_LcdCommand(0x29U);
	Boot_DelayMs(20U);
	Boot_UI_Clear();
}

void Boot_UI_Clear(void)
{
	Boot_LcdFillRect(0U, 0U, BOOT_LCD_WIDTH, BOOT_LCD_HEIGHT, BOOT_COLOR_BLACK);
}

void Boot_UI_Message(const char *title, const char *line1, const char *line2)
{
	Boot_UI_Clear();
	Boot_LcdFillRect(0U, 0U, BOOT_LCD_WIDTH, 34U, BOOT_COLOR_PANEL);
	Boot_LcdText(10U, 9U, title, BOOT_COLOR_ACCENT, BOOT_COLOR_PANEL, 2U);
	if (line1 != NULL)
	{
		Boot_LcdText(12U, 82U, line1, BOOT_COLOR_WHITE, BOOT_COLOR_BLACK, 2U);
	}
	if (line2 != NULL)
	{
		Boot_LcdText(12U, 116U, line2, BOOT_COLOR_GRAY, BOOT_COLOR_BLACK, 2U);
	}
}

static void Boot_FormatPercent(char *text, u8 percent)
{
	u8 hundreds = (u8)(percent / 100U);
	u8 tens = (u8)((percent / 10U) % 10U);
	u8 ones = (u8)(percent % 10U);
	u8 pos = 0U;

	if (hundreds != 0U)
	{
		text[pos++] = (char)('0' + hundreds);
	}
	if ((hundreds != 0U) || (tens != 0U))
	{
		text[pos++] = (char)('0' + tens);
	}
	text[pos++] = (char)('0' + ones);
	text[pos++] = '%';
	text[pos] = '\0';
}

void Boot_UI_Progress(const char *title, const char *detail, u8 percent)
{
	char percent_text[5];
	u16 fill_width;

	if (percent > 100U)
	{
		percent = 100U;
	}
	Boot_LcdFillRect(0U, 0U, BOOT_LCD_WIDTH, 34U, BOOT_COLOR_PANEL);
	Boot_LcdText(10U, 9U, title, BOOT_COLOR_ACCENT, BOOT_COLOR_PANEL, 2U);
	Boot_LcdFillRect(0U, 50U, BOOT_LCD_WIDTH, 130U, BOOT_COLOR_BLACK);
	Boot_LcdText(12U, 70U, detail, BOOT_COLOR_WHITE, BOOT_COLOR_BLACK, 2U);
	Boot_LcdFillRect(16U, 124U, 288U, 22U, BOOT_COLOR_GRAY);
	Boot_LcdFillRect(18U, 126U, 284U, 18U, BOOT_COLOR_BLACK);
	fill_width = (u16)((282UL * percent) / 100U);
	if (fill_width > 0U)
	{
		Boot_LcdFillRect(19U, 127U, fill_width, 16U,
			(percent == 100U) ? BOOT_COLOR_GREEN : BOOT_COLOR_ACCENT);
	}
	Boot_FormatPercent(percent_text, percent);
	Boot_LcdText(137U, 158U, percent_text, BOOT_COLOR_WHITE, BOOT_COLOR_BLACK, 2U);
}

void Boot_UI_Menu(u8 selected)
{
	static const char *items[4] =
	{
		"RESTORE BACKUP",
		"INSTALL UPDATE",
		"BACKUP APP",
		"BOOT APP"
	};
	u8 i;

	Boot_UI_Message("STAR GAMER", "BOOTLOADER", NULL);
	Boot_LcdFillRect(0U, 64U, BOOT_LCD_WIDTH, 154U, BOOT_COLOR_BLACK);
	for (i = 0U; i < 4U; i++)
	{
		u16 y = (u16)(68U + i * 34U);
		u16 bg = (i == selected) ? BOOT_COLOR_PANEL : BOOT_COLOR_BLACK;
		u16 fg = (i == selected) ? BOOT_COLOR_ACCENT : BOOT_COLOR_WHITE;
		Boot_LcdFillRect(10U, (u16)(y - 4U), 300U, 28U, bg);
		Boot_LcdText(18U, y, (i == selected) ? ">" : " ", fg, bg, 2U);
		Boot_LcdText(42U, y, items[i], fg, bg, 2U);
	}
}

u32 Boot_KeyRaw(void)
{
	u32 keys = 0U;

	if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_1) == GPIO_PIN_RESET)
	{
		keys |= BOOT_KEY_UP;
	}
	if (HAL_GPIO_ReadPin(GPIOA, GPIO_PIN_1) == GPIO_PIN_RESET)
	{
		keys |= BOOT_KEY_DOWN;
	}
	if (HAL_GPIO_ReadPin(GPIOE, GPIO_PIN_2) == GPIO_PIN_RESET)
	{
		keys |= BOOT_KEY_OK;
	}
	if (HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_7) == GPIO_PIN_RESET)
	{
		keys |= BOOT_KEY_CANCEL;
	}
	return keys;
}

u8 Boot_KeyHeld(u32 key, u32 duration_ms)
{
	u32 start = Boot_TickMs();

	while ((Boot_TickMs() - start) < duration_ms)
	{
		if ((Boot_KeyRaw() & key) == 0U)
		{
			return 0U;
		}
		Boot_DelayMs(1U);
	}
	return 1U;
}

u32 Boot_KeyWait(void)
{
	u32 key;

	do
	{
		key = Boot_KeyRaw();
		Boot_DelayMs(5U);
	} while (key == 0U);

	Boot_DelayMs(25U);
	key &= Boot_KeyRaw();
	while (Boot_KeyRaw() != 0U)
	{
		Boot_DelayMs(5U);
	}
	return key;
}

void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
	if (htim->Instance == TIM13)
	{
		Boot_TickInterrupt();
	}
}
