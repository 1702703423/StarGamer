#include "Arduboy_Port.h"

#include "LCD.h"
#include "Malloc.h"
#include "System_Status.h"
#include "cmsis_os2.h"
#include "key.h"
#include "main.h"

#include <string.h>

#define ARDUBOY_PORT_DELAY_SLICE_MS    10U
#define ARDUBOY_PORT_LCD_WIDTH         320U
#define ARDUBOY_PORT_CLEAR_ROWS        100U

extern RNG_HandleTypeDef hrng;

static u8 *s_rgb_buffer;
static volatile u8 s_stop_requested;
static u8 s_rng_failed;
static u32 s_prng_state = 0x7F4A7C15U;
static Arduboy_PortStats_t s_stats;

static void Arduboy_Port_Paint(const u8 *image);
static u8 Arduboy_Port_Buttons(void);
static u32 Arduboy_Port_Millis(void);
static void Arduboy_Port_Delay(u32 milliseconds);
static u32 Arduboy_Port_RandomWord(void);
static void Arduboy_Port_ClearDisplay(void);
static void Arduboy_Port_Convert(const u8 *image);

static const ArduboyCore_Platform_t s_platform =
{
	.paint = Arduboy_Port_Paint,
	.buttons = Arduboy_Port_Buttons,
	.millis = Arduboy_Port_Millis,
	.delay = Arduboy_Port_Delay,
	.random_word = Arduboy_Port_RandomWord,
	.should_stop = Arduboy_Port_ShouldStop,
};

int Arduboy_Port_Begin(void)
{
	if (s_rgb_buffer != NULL)
	{
		return OPERATE_FAILED;
	}

	s_rgb_buffer = (u8 *)sys_malloc_aligned(SRAMEX, ARDUBOY_PORT_RGB_BUFFER_SIZE, 32U);
	if (s_rgb_buffer == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}

	memset(&s_stats, 0, sizeof(s_stats));
	s_rng_failed = 0U;
	s_prng_state ^= HAL_GetTick() ^ 0xA5C31E27U;
	Arduboy_Port_ResetStop();
	ArduboyCore_SetPlatform(&s_platform);
	Arduboy_Port_ClearDisplay();
	return SYSTEM_STATUS_OK;
}

void Arduboy_Port_End(void)
{
	ArduboyCore_SetPlatform(NULL);
	if (s_rgb_buffer != NULL)
	{
		sys_free_aligned(SRAMEX, s_rgb_buffer);
		s_rgb_buffer = NULL;
	}
}

void Arduboy_Port_ResetStop(void)
{
	s_stop_requested = 0U;
}

void Arduboy_Port_RequestStop(void)
{
	s_stop_requested = 1U;
}

u8 Arduboy_Port_ShouldStop(void)
{
	if (s_stop_requested != 0U)
	{
		return 1U;
	}

	if ((KEY_GetState() & KEY_GAME_EXIT) != 0U)
	{
		s_stop_requested = 1U;
		return 1U;
	}

	return 0U;
}

void Arduboy_Port_RecordCpuLoad(u8 cpu_load)
{
	if (cpu_load > s_stats.max_cpu_load)
	{
		s_stats.max_cpu_load = cpu_load;
	}
}

void Arduboy_Port_GetStats(Arduboy_PortStats_t *stats)
{
	if (stats != NULL)
	{
		*stats = s_stats;
	}
}

static void Arduboy_Port_Paint(const u8 *image)
{
	u32 start;
	u32 elapsed;

	if (image == NULL || s_rgb_buffer == NULL || Arduboy_Port_ShouldStop() != 0U)
	{
		return;
	}

	start = HAL_GetTick();
	Arduboy_Port_Convert(image);
	elapsed = HAL_GetTick() - start;
	s_stats.last_convert_ms = elapsed;
	if (elapsed > s_stats.max_convert_ms)
	{
		s_stats.max_convert_ms = elapsed;
	}

	start = HAL_GetTick();
	LCD_DisplayArea(ARDUBOY_PORT_OUTPUT_X, ARDUBOY_PORT_OUTPUT_Y,
	                ARDUBOY_PORT_OUTPUT_X + ARDUBOY_PORT_OUTPUT_WIDTH - 1U,
	                ARDUBOY_PORT_OUTPUT_Y + ARDUBOY_PORT_OUTPUT_HEIGHT - 1U,
	                s_rgb_buffer);
	elapsed = HAL_GetTick() - start;
	s_stats.last_flush_ms = elapsed;
	if (elapsed > s_stats.max_flush_ms)
	{
		s_stats.max_flush_ms = elapsed;
	}
	s_stats.frame_count++;
}

static u8 Arduboy_Port_Buttons(void)
{
	u32 keys = KEY_GetState();
	u8 buttons = 0U;

	if ((keys & KEY_GAME_UP) != 0U) buttons |= UP_BUTTON;
	if ((keys & KEY_GAME_DOWN) != 0U) buttons |= DOWN_BUTTON;
	if ((keys & KEY_GAME_LEFT) != 0U) buttons |= LEFT_BUTTON;
	if ((keys & KEY_GAME_RIGHT) != 0U) buttons |= RIGHT_BUTTON;
	if ((keys & KEY_GAME_A) != 0U) buttons |= A_BUTTON;
	if ((keys & KEY_GAME_B) != 0U) buttons |= B_BUTTON;
	return buttons;
}

static u32 Arduboy_Port_Millis(void)
{
	return HAL_GetTick();
}

static void Arduboy_Port_Delay(u32 milliseconds)
{
	u32 slice;

	if (milliseconds == 0U)
	{
		(void)osThreadYield();
		return;
	}

	while (milliseconds > 0U && Arduboy_Port_ShouldStop() == 0U)
	{
		slice = (milliseconds > ARDUBOY_PORT_DELAY_SLICE_MS) ?
		        ARDUBOY_PORT_DELAY_SLICE_MS : milliseconds;
		(void)osDelay(slice);
		milliseconds -= slice;
	}
}

static u32 Arduboy_Port_RandomWord(void)
{
	u32 value;

	if (s_rng_failed == 0U)
	{
		if (HAL_RNG_GenerateRandomNumber(&hrng, &value) == HAL_OK)
		{
			return value;
		}
		s_rng_failed = 1U;
	}

	/* Xorshift32 is only a fallback for a peripheral fault. */
	value = s_prng_state;
	value ^= value << 13;
	value ^= value >> 17;
	value ^= value << 5;
	if (value == 0U)
	{
		value = 0x6D2B79F5U;
	}
	s_prng_state = value;
	return value;
}

static void Arduboy_Port_ClearDisplay(void)
{
	memset(s_rgb_buffer, 0, ARDUBOY_PORT_RGB_BUFFER_SIZE);
	LCD_DisplayArea(0U, 0U, ARDUBOY_PORT_LCD_WIDTH - 1U,
	                ARDUBOY_PORT_CLEAR_ROWS - 1U, s_rgb_buffer);
	LCD_DisplayArea(0U, ARDUBOY_PORT_CLEAR_ROWS, ARDUBOY_PORT_LCD_WIDTH - 1U,
	                ARDUBOY_PORT_CLEAR_ROWS * 2U - 1U, s_rgb_buffer);
	LCD_DisplayArea(0U, ARDUBOY_PORT_CLEAR_ROWS * 2U, ARDUBOY_PORT_LCD_WIDTH - 1U,
	                239U, s_rgb_buffer);
}

static void Arduboy_Port_Convert(const u8 *image)
{
	u32 source_x;
	u32 source_y;
	u32 source_index;
	u32 destination_index;
	u8 pixel;
	u8 color;
	u8 *row;
	u8 *duplicate_row;

	for (source_y = 0U; source_y < ARDUBOY_HEIGHT; source_y++)
	{
		row = s_rgb_buffer + source_y * 2U * ARDUBOY_PORT_OUTPUT_WIDTH * 2U;
		duplicate_row = row + ARDUBOY_PORT_OUTPUT_WIDTH * 2U;
		for (source_x = 0U; source_x < ARDUBOY_WIDTH; source_x++)
		{
			source_index = (source_y / 8U) * ARDUBOY_WIDTH + source_x;
			pixel = (u8)(1U << (source_y & 7U));
			color = ((image[source_index] & pixel) != 0U) ? 0xFFU : 0x00U;
			destination_index = source_x * 4U;
			row[destination_index] = color;
			row[destination_index + 1U] = color;
			row[destination_index + 2U] = color;
			row[destination_index + 3U] = color;
		}
		memcpy(duplicate_row, row, ARDUBOY_PORT_OUTPUT_WIDTH * 2U);
	}
}
