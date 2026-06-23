# Star Gamer

STM32F407ZG handheld game console — NES emulator with FreeRTOS, FATFS, LibJPEG, LCD, I2S audio.

## Project

- **MCU**: STM32F407ZGT6 (Cortex-M4F @ 168 MHz, 1 MB Flash, 192 KB SRAM)
- **Toolchain**: Keil MDK-ARM V5.32 (ARMCLANG / AC6)
- **Config**: CubeMX-generated (`Star Gamer.ioc`), HAL driver layer
- **Entry point**: `Core/Src/main.c` → `main()` → init peripherals → `osKernelStart()`

## Commands

- **Build**: Open `MDK-ARM/Star Gamer.uvprojx` in Keil uVision5, press **F7** (or Project → Build Target)
- **Flash / Debug**: **F8** (or Flash → Download) with ST-Link/DAP-Link; use Debug (`Ctrl+F5`) for an interactive session
- **Clean**: Project → Clean Targets (no CLI equivalent)

> No Makefile or CMake project. Build is fully managed inside the Keil IDE. `MDK-ARM/reasonix.toml` records CubeMX/ArmClang metadata.

## Architecture

| Module | Path | Role |
|--------|------|------|
| **Core** | `Core/Src/`, `Core/Inc/` | `main.c` (entry, peripheral init), `freertos.c` (defaultTask), `stm32f4xx_it.c` (ISRs), HAL MSP/timebase |
| **Drivers** | `Drivers/` | CMSIS, STM32F4 HAL, `LCD/` (ILI93xx SPIFSMC display) |
| **System** | `System/` | `sys.h` (ubiquitous type/bit-band macros), `Malloc`, `System_Status`, `adt_chain` |
| **Middlewares** | `Middlewares/Third_Party/` | FreeRTOS v10.3.1 (CMSIS V2), FatFs (SDIO, codepage 936), LibJPEG, LVGL (embedded GUI library), **NES emulator** (6502 + PPU + APU + 100+ mappers) |
| **GUI** | `GUI/` | `GUI_Task.c` — LVGL RTOS thread (`osPriorityAboveNormal`, 2048-byte stack), header at `MDK-ARM/GUI_Task.h` |
| **BSP** | `BSP/` | `w25q64` SPI flash, `i2s` audio out |
| **FATFS / LIBJPEG** | `FATFS/`, `LIBJPEG/` | CubeMX-generated shim layers (App + Target) |
| **Debug** | `Debug/` | Letter Shell v3.1.2 (UART command console), UART1 printf redirect, test harness |
| **ELF Loader** | `ELF Loader/` | Dynamic loading (`dl_lib`) for on-device code loading |
| **Media** | `Media/` | `Player.c` — audio/file player integration |
| **IAP** | `IAP/` | In-Application Programming stubs (`.c`/`.h` empty) |

## Conventions

- **Types**: `u8`/`u16`/`u32` and `s8`/`s16`/`s32` are the project standard (via `System/sys.h`). Use these, not `uint8_t` etc.
- **CubeMX safety**: Code-gen owns the regions between `/* USER CODE BEGIN … */` / `/* USER CODE END … */` pairs. Put all custom logic in USER blocks; do NOT modify generated code outside them, or it will be lost on re-generation.
- **Thread API**: Use CMSIS RTOS V2 (`cmsis_os2.h`): `osThreadNew`, `osKernelInitialize`, `osKernelStart`. Static thread buffers are the norm (pre-allocated stacks).
- **GPIO**: Bit-band macros (`PAout(n)`, `PBin(n)`, etc.) from `sys.h` for fast single-pin I/O.
- **printf**: UART1 via `MDK-ARM/UART1_Debug.c` (semihosting disabled, requires MicroLIB).
- **Comments**: Chinese (GB2312/GBK) used throughout alongside English; source encoding is likely GBK.
- **File naming**: Mixed case for most modules (`Player.c`, `LCD.c`, `Malloc.c`); NES mapper files are numeric (e.g. `001.cpp`).
- **LVGL**: Config at `Middlewares/Third_Party/LVGL/GUI/lvgl/lv_conf.h`; runs in its own CMSIS RTOS V2 thread (`GUI_Handler` in `GUI/GUI_Task.c`). LVGL tick is likely driven by a FreeRTOS timer or the GUI thread itself.

## Notes

-
