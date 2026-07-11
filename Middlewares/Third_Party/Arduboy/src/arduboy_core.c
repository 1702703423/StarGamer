#include "arduboy_core.h"

#include <string.h>

static ArduboyCore_Platform_t s_platform;

void ArduboyCore_SetPlatform(const ArduboyCore_Platform_t *platform)
{
	if (platform == NULL)
	{
		memset(&s_platform, 0, sizeof(s_platform));
		return;
	}

	s_platform = *platform;
}

void ArduboyCore_Paint(const u8 *image)
{
	if (image != NULL && s_platform.paint != NULL)
	{
		s_platform.paint(image);
	}
}

u8 ArduboyCore_Buttons(void)
{
	return (s_platform.buttons != NULL) ? s_platform.buttons() : 0U;
}

u32 ArduboyCore_Millis(void)
{
	return (s_platform.millis != NULL) ? s_platform.millis() : 0U;
}

void ArduboyCore_Delay(u32 milliseconds)
{
	if (s_platform.delay != NULL)
	{
		s_platform.delay(milliseconds);
	}
}

s32 ArduboyCore_Random(s32 minimum, s32 maximum)
{
	u32 value;
	u32 range;
	u32 threshold;

	if (minimum >= maximum || s_platform.random_word == NULL)
	{
		return minimum;
	}

	/* Unsigned subtraction preserves the intended span for negative minima. */
	range = (u32)maximum - (u32)minimum;
	if (range == 0U)
	{
		return minimum;
	}

	threshold = (0U - range) % range;
	do
	{
		value = s_platform.random_word();
	} while (value < threshold);

	return (s32)((u32)minimum + (value % range));
}

u8 ArduboyCore_ShouldStop(void)
{
	return (s_platform.should_stop != NULL) ? s_platform.should_stop() : 0U;
}

u8 buttonsState(void)
{
	return ArduboyCore_Buttons();
}

void paintScreen(const u8 *image)
{
	ArduboyCore_Paint(image);
}

u8 width(void)
{
	return (u8)ARDUBOY_WIDTH;
}

u8 height(void)
{
	return (u8)ARDUBOY_HEIGHT;
}
