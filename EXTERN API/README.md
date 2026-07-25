# Star Gamer AXF API

Add `ELF Loader/common`, `ELF Loader`, and `EXTERN API` to the external AXF
project include paths, then include `stargamer_axf_api.h`.

An AXF application must acquire the LCD before drawing and release it before
returning. PB7 is reserved as the launcher exit key. `SG_ShouldExit()` allows
cooperative cleanup; the firmware supervisor will terminate an application
that does not return.

```c
#include "stargamer_axf_api.h"

DL_EXPORT int main(int argc, char *argv[])
{
	(void)argc;
	(void)argv;

	if (SG_LCD_Acquire() != SG_API_OK)
	{
		return -1;
	}
	SG_LCD_Clear(0x0000U);
	while (SG_ShouldExit() == 0U)
	{
		/* Application loop. */
	}
	SG_LCD_Release();
	return 0;
}
```

RGB565 arguments use host-endian `uint16_t` values. The host API converts
them to the ST7789 wire byte order and copies each scanline to a DMA-safe
internal staging buffer, so AXF source pixels may reside in CCM or SRAMEX.
