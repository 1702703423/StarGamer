# CLAUDE.md

> 此文件为 Claude Code 提供详细参考。Reasonix 用户请参阅 [`AGENTS.md`](./AGENTS.md)，其中包含精简版的中文项目记忆。

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Star Gamer** — STM32F407ZG-based handheld game console running a full-featured NES emulator with FreeRTOS, LVGL GUI, I2S audio, and SD card filesystem.

- **MCU**: STM32F407ZGT6 (Cortex-M4F @ 168 MHz, 1 MB Flash, 192 KB internal SRAM + 1 MB external SRAM)
- **Toolchain**: Keil MDK-ARM V5.32 with ARMCLANG (AC6 compiler)
- **RTOS**: FreeRTOS v10.3.1 with CMSIS-RTOS V2 API
- **Configuration**: STM32CubeMX-generated (`Star Gamer.ioc`), HAL driver layer

## Build Commands

**This project uses Keil uVision IDE — there is no Makefile or command-line build system.**

- **Build**: Open `MDK-ARM/Star Gamer.uvprojx` in Keil uVision5, press **F7** or Project → Build Target
- **Flash/Debug**: Press **F8** (Flash → Download) with ST-Link or DAP-Link debugger
- **Clean**: Project → Clean Targets from the IDE menu
- **Interactive Debug**: Press **Ctrl+F5** to start a debugging session

The project configuration and build settings are stored in `MDK-ARM/Star Gamer.uvprojx` (XML project file). Post-build actions automatically generate `.bin` files from the ELF output.

## Architecture

### Directory Structure

| Path | Purpose |
|------|---------|
| `Core/Src/`, `Core/Inc/` | Entry point (`main.c`), peripheral initialization, FreeRTOS task creation (`freertos.c`), interrupt handlers (`stm32f4xx_it.c`), HAL MSP/timebase |
| `Drivers/` | CMSIS headers, STM32F4xx HAL drivers, **LCD driver** (ILI93xx via FSMC parallel interface) |
| `System/` | Core utilities: `sys.h` (ubiquitous type definitions, bit-band macros), `Malloc` (multi-bank memory allocator for CCM + external SRAM), `System_Status` (system state, error codes, resource tables), `adt_chain` (linked list ADT) |
| `BSP/` | Board support: `w25q64` (SPI NOR flash driver), `i2s` (I2S3 audio output via DMA), `at24c02` (I2C EEPROM driver) |
| `Middlewares/Third_Party/` | External components (see below) |
| `FATFS/`, `LIBJPEG/` | CubeMX-generated middleware shims (App + Target layers) |
| `GUI/` | LVGL GUI thread (`GUI_Task.c`) — runs as FreeRTOS task with `osPriorityAboveNormal`, 2048-byte stack |
| `Media/` | `Player.c` — audio playback orchestration (WAV file handling, I2S feeding) |
| `Debug/` | **Letter Shell v3.1.2** UART command console, UART1 printf redirect, test harness (`test.c`/`test.h`) |
| `ELF Loader/` | Dynamic ELF loader (`dl_lib`) for runtime code loading from SD card |
| `IAP/` | In-Application Programming stubs (currently empty) |

### Middleware Components

- **FreeRTOS v10.3.1**: CMSIS-RTOS V2 API (`cmsis_os2.h`). Static task allocation is preferred. Config: `Core/Inc/FreeRTOSConfig.h`
- **FatFs**: SD card filesystem via SDIO + DMA, codepage 936 (GBK Chinese support)
- **LibJPEG**: JPEG decoding for image assets
- **LVGL**: Lightweight embedded GUI library. Config: `Middlewares/Third_Party/LVGL/GUI/lvgl/lv_conf.h`. Application code: `Middlewares/Third_Party/LVGL/GUI_APP/`
- **NES Emulator**: Full NES/Famicom emulator with 6502 CPU, PPU, APU, and **240+ mapper implementations** (in `Middlewares/Third_Party/NES/mapper/`). Core files: `nes_main.c`, `nes_ppu.c`, `nes_apu.c`, `nes_mapper.c`, `nes_crc32.c`. Originally ported from ye781205's work, adapted by ALIENTEK.

### Key Subsystems

#### Memory Architecture

- **Internal SRAM**: 192 KB (128 KB main + 64 KB CCM)
  - CCM (Core-Coupled Memory): 60 KB dedicated bank at 0x10000000, **CPU-only access** (no DMA). Managed by `Malloc` as `SRAMCCM` bank
- **External SRAM**: 1 MB FSMC SRAM at 0x68000000–0x680FFFFF. Managed by `Malloc` as `SRAMEX` bank (963 KB usable, 32-byte blocks)
- **Frame buffers**: 150 KB LCD screen buffer allocated from external SRAM
- **Memory allocator** (`System/Malloc.c`): Multi-bank allocator with `mymalloc(memx, size)` / `myfree(memx, ptr)`. `memx` selects `SRAMCCM` (0) or `SRAMEX` (1)

#### Display System

- **LCD**: ST7789 (240×320 pixels, 16-bit RGB565)
- **Interface**: FSMC parallel (fast memory-mapped writes)
- **Driver**: `Drivers/LCD/LCD.c`, header `Drivers/LCD/LCD.h`
- **Orientation**: `USE_HORIZONTAL` set to 1 (90° rotation, landscape mode)
- **Backlight**: PWM-controlled via TIM12 CH2

#### Audio System

- **Hardware**: I2S3 (via SPI3) + DMA streaming to external DAC
- **Driver**: `BSP/i2s.c` — unified "audio card" API with volume control, sample rate switching, double-buffered DMA
- **API**: `audio_card_init()`, `audio_card_open()`, `audio_card_write()`, `audio_card_register_tx()` (callback on buffer complete)
- **Player**: `Media/Player.c` — handles WAV file parsing and feeds PCM data to I2S DMA buffers
- **NES Audio**: APU channels (pulse, triangle, noise, DMC) rendered in `nes_apu.c`, mixed and pushed to I2S via registered callback

#### Storage

- **SD Card**: SDIO 4-bit mode + DMA (RX: DMA2_Stream3, TX: DMA2_Stream6). FatFs layer in `FATFS/`
- **SPI Flash**: W25Q64 (8 MB) via SPI1 — available for asset storage, not currently used for filesystem
- **EEPROM**: AT24C02 (256 bytes) via I2C — suitable for save states, configuration

### NES Emulator Integration

- **Entry**: `Middlewares/Third_Party/NES/nes_main.c` — ROM loading, emulation loop
- **Frame skipping**: `NES_SKIP_FRAME` defined as 2 (renders every 3rd frame for performance)
- **Display integration**: PPU renders to internal framebuffer, then copied to LCD via `LCD.c` functions
- **Audio integration**: APU sample generation synchronized with emulation loop, fed to I2S driver via callback registered in `BSP/i2s.c`
- **Mappers**: 100+ mapper implementations in `Middlewares/Third_Party/NES/mapper/` (numeric files: `000.cpp`–`255.cpp`). Mapper selected at ROM load based on iNES header

## Coding Conventions

### Type Definitions

**Use the project's custom types exclusively** (defined in `System/sys.h`):

- Unsigned: `u8`, `u16`, `u32` (NOT `uint8_t`, `uint16_t`, etc.)
- Signed: `s8`, `s16`, `s32`
- NES emulator: defines its own types (`int8`, `uint8`, `int16`, `uint16`, etc.) in `nes_main.h`

### GPIO Macros

Fast bit-band GPIO access via `sys.h`:

- **Output**: `PAout(n)`, `PBout(n)`, `PCout(n)`, etc. — set pin n of port A/B/C
- **Input**: `PAin(n)`, `PBin(n)`, `PCin(n)`, etc. — read pin n
- Example: `PBout(15) = 1;` sets GPIOB pin 15 high

### RTOS API

- **Use CMSIS-RTOS V2** (`cmsis_os2.h`), NOT native FreeRTOS API
- Thread creation: `osThreadNew(func, arg, &attr)` with `osThreadAttr_t` defining stack size, priority, name
- Prefer **static allocation**: pre-allocate stack buffers and control blocks as shown in `main.c` `defaultTask` example
- Synchronization: `osMutexNew()`, `osSemaphoreNew()`, `osMessageQueueNew()`
- Thread management helper: `SysCreateThread()` in `System/System_Status.c` wraps `osThreadNew()` with resource tracking

### Memory Allocation

- **Dynamic allocation**: `mymalloc(memx, size)` / `myfree(memx, ptr)` from `System/Malloc.h`
  - `memx = SRAMCCM` (0): Use for CPU-only data (no DMA buffers!)
  - `memx = SRAMEX` (1): Use for DMA buffers, large allocations, shared data
- **Frame buffers**: Call `creat_frame_buffer(&code)` from `System/System_Status.c` — allocates from external SRAM, registers in resource table

### CubeMX Integration

**Critical**: Respect CubeMX `USER CODE BEGIN` / `USER CODE END` blocks

- All custom code in generated files (`main.c`, `stm32f4xx_it.c`, `*_msp.c`) MUST go inside `/* USER CODE BEGIN X */` / `/* USER CODE END X */` pairs
- Code outside these blocks will be **overwritten** when CubeMX regenerates
- Re-generation triggered by editing `Star Gamer.ioc` in CubeMX

### Comments and Encoding

- **Language**: Mix of Chinese (GBK/GB2312) and English throughout codebase
- **Source encoding**: GBK (Chinese characters present in comments, strings, docstrings)
- When adding comments, match the surrounding file's style (English preferred for new code, but Chinese acceptable)

### Naming

- **Files**: Mixed case, descriptive (`Player.c`, `Malloc.c`, `LCD.c`)
- **Functions**: `snake_case` for system/BSP, `CamelCase` for HAL-related, `nes_lowercase` for NES emulator
- **Structs**: `typedef struct Name { ... } Name_t;` — always typedef with `_t` suffix
- **Defines**: `UPPER_SNAKE_CASE`

## Debug and Utilities

### Letter Shell (UART Console)

- **Version**: 3.1.2 (NevermindZZT@gmail.com)
- **Interface**: UART1 (configured for printf as well)
- **Files**: `Debug/shell.c`, `Debug/shell.h`, `Debug/shell_cfg.h`
- **Support**: Filesystem integration in `Debug/fs_support/`, custom command extensions in `Debug/shell_ext.c`
- **Initialization**: Shell instance declared in `main.c` as `Shell shell;`, initialized in user code

### Printf Redirection

- **Target**: UART1
- **Implementation**: `MDK-ARM/UART1_Debug.c` (check this directory for the file)
- **Requirement**: MicroLIB must be enabled in Keil project settings (semihosting disabled)

### Error Handling

System error codes defined in `System/System_Status.h`:

- `SYSTEM_STATUS_OK` (0x00)
- `RTOS_UNEXPECTION_FATAL`, `STACK_OVERFLOW`, `OUT_OF_HEAP_MEMORY`, `MEMORY_TEST_FAILED`, etc.

Global status structures:

- `System_Status_Information_t SSI` — filesystem mount, fatal errors
- `System_Core_Information_t SCI` — system config (password, backup, debug mode)
- `System_Resource_Table_t SRT` — linked lists of allocated frame buffers and task handles

## Working with the NES Emulator

### Adding a New Mapper

1. Create `Middlewares/Third_Party/NES/mapper/XXX.cpp` and `XXX.h` (where XXX is the mapper number, zero-padded to 3 digits)
2. Follow the pattern from existing mappers (e.g., `000.cpp`, `001.cpp`)
3. Implement required interface functions: mapper initialization, bank switching, PPU hooks
4. Add mapper to the dispatch table in `nes_mapper.c`

### Modifying Audio Output

- **APU rendering**: `Middlewares/Third_Party/NES/nes_apu.c` — generates samples per emulation cycle
- **Buffer feeding**: Register callback via `audio_card_register_tx()` in `BSP/i2s.c`
- **Sample rate**: Configured in `audio_card_open()` call (typically 44.1 kHz or 48 kHz)
- **Buffer size**: Double-buffered (1024 bytes × 2), configured via `audio_play_info_t` in `i2s.h`

## Common Pitfalls

1. **CCM SRAM DMA restriction**: Never allocate DMA buffers from `SRAMCCM` bank — DMA controllers cannot access CCM. Always use `SRAMEX` for DMA.
2. **CubeMX regeneration**: Always place custom code in `USER CODE` blocks in generated files
3. **Type mismatches**: Use `u8`/`u16`/`u32`, not standard `uint8_t` types, to match project style
4. **RTOS API**: Use CMSIS-RTOS V2 (`cmsis_os2.h`), not native FreeRTOS calls
5. **Encoding issues**: Be aware of GBK encoding in source files if editing Chinese comments or strings
6. **External SRAM**: Remember external SRAM starts at 0x68000000, not 0x20000000 (internal SRAM)
