# CLAUDE.md

> 此文件为 Claude Code 提供英文参考。Reasonix 用户请参阅 [`AGENTS.md`](./AGENTS.md)，其中包含最新的中文项目记忆。

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

**Star Gamer** — STM32F407ZG-based handheld game console: NES emulator, Arduboy games, AVI video player, MP3/WAV music player. Integrates FreeRTOS, LVGL, FatFs, LibJPEG, Helix MP3, I2S audio, USB MSC.

- **MCU**: STM32F407ZGT6 (Cortex-M4F @ 168 MHz, 1 MB Flash, 192 KB internal SRAM + 1 MB external SRAM)
- **Toolchain**: Keil MDK-ARM V5.32 with ARMCLANG (AC6 compiler)
- **RTOS**: FreeRTOS v10.3.1 with CMSIS-RTOS V2 API
- **Configuration**: Dual CubeMX projects — main firmware `Star Gamer.ioc` + BootLoader `BootLoader/StarGamerBootLoader/StarGamerBootLoader.ioc`

## Build Commands

**This project uses Keil uVision IDE — there is no Makefile or command-line build system.**

- **Main firmware build**: Open `MDK-ARM/Star Gamer.uvprojx` in Keil uVision5, press **F7** or Project → Build Target
- **BootLoader build**: Open `BootLoader/StarGamerBootLoader/MDK-ARM/StarGamerBootLoader.uvprojx`, press **F7**
- **Flash/Debug**: Press **F8** (Flash → Download) with ST-Link or DAP-Link debugger
- **Clean**: Project → Clean Targets from the IDE menu
- **Interactive Debug**: Press **Ctrl+F5** to start a debugging session

Post-build actions automatically generate `.bin` files from the ELF output.

## Architecture

### Directory Structure

| Path | Purpose |
|------|---------|
| `Core/Src/`, `Core/Inc/` | Entry point (`main.c`), peripheral init, FreeRTOS task creation (`freertos.c`), ISRs (`stm32f4xx_it.c`), HAL MSP/timebase |
| `Drivers/` | CMSIS headers, STM32F4xx HAL drivers, **LCD driver** (ST7789 SPI with RGB565 block transfer/scaling, DMA timeout handling) |
| `System/` | Core utilities: `sys.h` (ubiquitous types, bit-band macros), `Malloc` (dual-bank CCM+SRAMEX allocator), `System_Status` (state/errors/resources/settings), `firmware_start.c` (init + HardFault LCD dump), `adt_chain` (linked list), `kfifo` (Linux-style generic kernel FIFO for PCM audio) |
| `BSP/` | Board support: `w25q64` (SPI NOR Flash), `i2s` (I2S3 audio+DMA with collision stats), `at24c02` (I2C EEPROM), `key` (8-button driver) |
| `Middlewares/` | FreeRTOS, FatFs (SDIO, codepage 936), LibJPEG (custom `jpeg_memory_provider`), LVGL, NES emulator, Arduboy compat layer, **Helix MP3** (ARM Cortex-M4 fixed-point, 24 source files incl. ARM asm) |
| `GUI/` | LVGL page manager: `GUI_Manage`, `GUI_Main`, `GUI_NES`, `GUI_Arduboy`, `GUI_Player` (with Seek UI), `GUI_Image`, `GUI_Text`, `GUI_Settings`, `GUI_Video`, `GUI_Font` (install+uninstall), `GUI_FileManage`, `GUI_Start` |
| `Media/` | Player subsystem: `Player.c` (orchestration, codec abstraction, Seek), `Player_Codec.h` (`read_pcm`/`seek`/`tell_seconds`/`close`), `Player_WAV`, `Player_MP3` (Helix wrapper), `Video.c` (AVI state machine, A/V sync, frame drop, deadline scheduling), `AVI_Parser` (chunk tolerance, Seek), `Video_MJPEG` (tunable stripe height), `Video_PCM` (32 KB kfifo + DMA double-buffer), `Video_Memory` (77 KB aligned pool) |
| `BootLoader/` | Independent CubeMX project (64 KB bare-metal firmware updater, Flash sectors 0-3), pre-built `.hex`/`.bin`/`.axf` available |
| `USB_DEVICE/` | USB MSC device stack (`App/` + `Target/`), SD card as USB mass storage, ST official USB device library |
| `Debug/` | Letter Shell v3.1.2 UART console, `shell_ext.c`, `shell_companion.c`, `fs_support/`, `videostat`/`audiostat` perf commands |
| `ELF Loader/` | Dynamic ELF loader (`dl_lib`, `dl_elf.h`, `dl_arch.c`) for runtime `.axf` loading from SD card, DLL-like mechanism for Cortex-M |
| `APP/Arduboy/` | Arduboy game runtime (`Arduboy_Runtime` state machine, `Arduboy_Port` HW layer, `Arduboy_Games` registry) |
| `FATFS/`, `LIBJPEG/` | CubeMX-generated middleware shims (App + Target) |
| `IAP/` | In-Application Programming: CRC32, EEPROM dual-slot records, W25Q64 staging/backup, vector table validation, progress callback |
| `EXTERN API/` | External API interface (empty directory, planned) |
| `PCB/` | LCEDA hardware design project (`ProPrj_StarGamer_*.epro2`) |
| `Docs/` | Design docs: AVI player (full design + tearing analysis + high-bitrate optimization), BootLoader, Arduboy port, Helix MP3, CCM stack, GUI SRAM, SRAMEX optimization |
| `Tools/` | PC dev tools: `hexdump` (hex viewer, C+Python), `map_analyzer` (ARMCLANG .map memory analyzer, C+Python tkinter + PyInstaller .exe) |
| `Tests/AVI_Parser_Host/` | AVI parser PC-hosted regression tests (boundary-condition unit tests + real AVI samples under `Tests/样本/`) |

### Boot Sequence

1. `main()` → HAL init, clock config (HSE 8MHz → PLL 168MHz), peripheral init
2. `osKernelInitialize()` → allocate defaultTask stack from CCM (4096 B) → `osKernelStart()`
3. `defaultTask` → `init()` (`System/firmware_start.c`): SRAMCCM+SRAMEX pool init, SD card FatFs mount, Shell init, splash screen (`GUI_Start_Show`), subsystem registration

### GUI Page Manager

Unified page architecture based on `GUI_Manage.c/h`:

| Page ID | File | Function |
|---------|------|----------|
| `GUI_PAGE_MAIN_MENU` | `GUI_Main.c` | 9-grid main menu |
| `GUI_PAGE_NES` | `GUI_NES.c` | NES game browser & launcher |
| `GUI_PAGE_ARDUBOY` | `GUI_Arduboy.c` | Arduboy game list (Buttons/Dino/FlappyBall) |
| `GUI_PAGE_MUSIC` | `GUI_Player.c` | Music player (WAV/MP3, with Seek progress UI) |
| `GUI_PAGE_IMAGE` | `GUI_Image.c` | JPEG image viewer |
| `GUI_PAGE_TEXT` | `GUI_Text.c` | Text viewer (UTF-8/GBK, glyph fallback) |
| `GUI_PAGE_SETTINGS` | `GUI_Settings.c` | System settings |
| `GUI_PAGE_VIDEO` | `GUI_Video.c` | AVI video player (MJPEG+PCM, with Seek) |
| `GUI_PAGE_FONT` | `GUI_Font.c` | XBF font installation & uninstallation |

- **Splash**: `GUI_Start.c` — startup spinner + status text, ready callback support
- **File browser**: `GUI_FileManage.c/h` — reusable FatFs browser, async pagination (`GUI_FILEMANAGE_MAX_ITEMS=32`)
- **Lifecycle**: `Page_Create` → `Page_Show` / `Page_Show_Anim` → `Page_Back` → `Page_Delete`
- **GUI thread**: 4096 B stack, `osPriorityAboveNormal`

### Media Player Subsystem

| Module | File | Role |
|--------|------|------|
| **Player core** | `Player.c/h` | Orchestration, codec abstraction, audio pipeline, Seek, position callbacks |
| **Codec interface** | `Player_Codec.h` | Abstract `read_pcm` / `seek` / `tell_seconds` / `close` |
| **WAV decoder** | `Player_WAV.c/h` | PCM WAV parsing (refactored as codec) |
| **MP3 decoder** | `Player_MP3.c/h` | Helix MP3 wrapper (5120 B window, stats system) |
| **Video framework** | `Video.c/h` | AVI playback state machine, A/V sync (PCM clock), dedicated `VideoAudioFeedTask`, frame drop, deadline scheduling |
| **AVI parser** | `AVI_Parser.c/h` | RIFF/AVI container, little-endian reads, chunk tolerance, Seek |
| **MJPEG decode** | `Video_MJPEG.c/h` | MJPEG frame decode pipeline, LibJPEG streaming, tunable stripe height, JPEG arena peak stats |
| **PCM audio** | `Video_PCM.c/h` | AVI PCM decode, 32 KB kfifo buffering, DMA double-half-buffer, underrun/collision stats |
| **Video memory** | `Video_Memory.c/h` | Frame buffer pool (SRAMEX), stripe buffers, 77 KB aligned allocator |

### Memory Architecture

- **Internal SRAM**: 192 KB (128 KB main + 64 KB CCM)
  - Main SRAM (0x20000000): `.data/.bss`, FreeRTOS `ucHeap`, static globals
  - CCM (0x10000000): CPU-only, **no DMA**. Managed as `SRAMCCM` (0). Used for RTOS task stacks via `sys_thread_stack_alloc()`
- **External SRAM**: 1 MB FSMC SRAM (0x68000000–0x680FFFFF). Managed as `SRAMEX` (1) (963 KB usable, 32-byte blocks). LVGL heap, frame buffers, GUI page contexts, video buffers, PCM FIFO
- **Allocator**: `mymalloc(memx, size)` / `myfree(memx, ptr)`, `memx` = `SRAMCCM` (0) or `SRAMEX` (1). `SRAMBANK` fixed at 2 (`SRAMMAIN` deprecated and removed)
- **Video memory**: 80 KB on-chip SRAM reserved segment (0x2000C000–0x2001FFFF, `RW_VIDEO_SRAM`); Video_Memory allocates 77 KB + 38 KB JPEG arena from SRAMEX; 32 KB PCM FIFO + 4 KB I/O buffer + I2S DMA double buffer in SRAMEX
- **Stack recycling**: Temporary threads (image decode, NES, Arduboy) reclaim ~20 KB CCM via `sys_thread_stack_free()`
- **BootLoader reservation**: Flash low 64 KB (sectors 0-3) reserved for BootLoader; App starts at 0x08010000

### Key Subsystems

- **LCD**: ST7789 240×320 RGB565, **SPI interface** (DMA writes). Driver `Drivers/LCD/LCD.c`, extended RGB565 block transfer & crop/scale. Backlight PWM (TIM12 CH2), `USE_HORIZONTAL = 1`. DMA timeout handling improved
- **Audio**: I2S3 (SPI3) + DMA → external DAC. `BSP/i2s.c` "sound card" API: `audio_card_init()`, `audio_card_open_prepare()`, `audio_card_open()`, `audio_card_write()`, double-buffered DMA with callback (`audio_card_register_tx()`), collision stats
- **Storage**: SD card SDIO 4-bit + DMA; W25Q64 8 MB SPI Flash (font 2MB + reserved 4MB + FW staging 1MB + FW backup 1MB); AT24C02 256B I2C EEPROM (IAP dual-slot records + system settings). FatFs with `ENABLE_SCRATCH_BUFFER` for DMA alignment
- **NES Emulator**: Entry `nes_main.c`, core `nes_ppu.c`/`nes_apu.c`/`nes_mapper.c`/`6502.s`, `NES_SKIP_FRAME = 2`, 240+ mappers. Rendering adapted for this hardware
- **Arduboy**: `Middlewares/Third_Party/Arduboy/` Arduboy2 engine core + 3 games. 128×64 1bpp framebuffer, runtime via `APP/Arduboy/Arduboy_Runtime.c`, port layer in `Arduboy_Port.c`
- **Helix MP3**: `Middlewares/libhelix/` fixed-point Layer III decoder. 14 C files + 2 ARM asm files + port adapter `port/helix_support.c`. Adapted via `Player_MP3.c` implementing the codec interface
- **USB MSC**: `USB_DEVICE/`, SD card as USB mass storage via CubeMX USB OTG FS + ST official USB device library
- **Font**: XBF font loaded from SD to SPI Flash (0x000000, 2 MB reserved). `myFont.c` proxy `lv_font_t`. Font uninstallation supported. Parameters persisted in `System_Settings_t`
- **HardFault**: `firmware_start.c` — standalone handler reinitializes SPI1+LCD GPIO, draws register dump on LCD. Memory-allocation HardFault fixed
- **Buttons**: `BSP/key.c` 8-button, bitmask macros, game aliases (`KEY_GAME_UP/DOWN/LEFT/RIGHT/OK/A/B/SELECT/START`). Button adaptation complete
- **IAP**: `IAP/IAP.c` — `IAP_PrepareUpdate()` from SD `update.bin` → CRC32 verify → W25Q64 staging → EEPROM dual-slot record. Power-loss recovery, vector table validation, progress callback. Shared protocol in `IAP_Protocol.h` used by both App and BootLoader

### BootLoader

Independent CubeMX project at `BootLoader/StarGamerBootLoader/`. Design doc: `Docs/BootLoader-Design.md`.

- **Flash layout**: BootLoader 64 KB (sectors 0-3, 0x08000000–0x0800FFFF), Application 960 KB (sectors 4-11, 0x08010000–0x080FFFFF)
- **Update flow**: SD `update.bin` → W25Q64 staging (0x600000) → CRC32 verify → erase internal Flash → verify → jump to App
- **Safety**: Dual backup (W25Q64 backup at 0x700000), EEPROM dual-slot records, power-loss tolerance, CRC throughout
- **Pre-built**: `.hex`, `.bin`, `.axf` binaries available

### LVGL Integration

- **LVGL thread**: `lvgl_timer_task`, 8192 B stack, `osPriorityNormal`, calls `lv_timer_handler()` every 10ms
- **Display driver**: `lv_port_disp_template.h`, external SRAM frame buffer
- **Input**: `lv_port_indev_init()` for button input
- **Video playback**: `lvgl_stop_ui()` / `lvgl_start_ui()` to release/reclaim LCD/SPI1
- **Startup safety**: Fixed HardFault caused by LVGL/GUI startup thread-static initialization

### ELF Dynamic Loader

`ELF Loader/` provides runtime dynamic loading for Cortex-M:

- **Core**: `dl_lib.c/h` — load/unload/symbol lookup (`dl_load_lib`, `dl_get_func`, `dl_destroy_lib`)
- **ELF parsing**: `dl_elf.h` — full ELF format structures
- **Arch adapter**: `dl_arch.c` — relocation instruction handling
- **Port layer**: `dl_port.c/h` — memory/file IO abstraction
- **User API**: `dl_user_api.h`

### AVI Video Player Status

Current player supports MJPEG + PCM A/V sync. Known issues and optimization directions in `Docs/AVI高码率卡顿优化方案.md`:

- **Symptoms**: High-bitrate DMA collision rate ~18-21%, audio DMA events may be lost, JPEG decode peaks at 66-75ms
- **Fix direction**: P0 — fix audio DMA event loss and lookahead queue blocking; P1 — further reduce JPEG CPU time
- **Tearing**: No TE sync signal; root cause analysis in `Docs/AVI视频撕裂问题结论.md`

## Coding Conventions

### Type Definitions

**Use project custom types exclusively** (defined in `System/sys.h`):

- Unsigned: `u8`, `u16`, `u32` (NOT `uint8_t`, `uint16_t`, etc.)
- Signed: `s8`, `s16`, `s32`
- NES emulator: defines own types (`int8`, `uint8`, etc.) in `nes_main.h`

### GPIO Macros

Fast bit-band GPIO access via `sys.h`:

- **Output**: `PAout(n)`, `PBout(n)`, `PCout(n)`, etc.
- **Input**: `PAin(n)`, `PBin(n)`, `PCin(n)`, etc.

### RTOS API

- **Use CMSIS-RTOS V2** (`cmsis_os2.h`), NOT native FreeRTOS API
- Static allocation: pre-allocate stack buffers and control blocks
- Thread stacks from CCM via `sys_thread_stack_alloc()`
- Temporary thread stacks recyclable via `sys_thread_stack_free()` from parent context
- Synchronization: `osMutexNew()`, `osSemaphoreNew()`, `osMessageQueueNew()`

### Memory Allocation

- `mymalloc(memx, size)` / `myfree(memx, ptr)` from `System/Malloc.h`
  - `memx = SRAMCCM` (0): CPU-only data (task stacks, local buffers) — **no DMA!**
  - `memx = SRAMEX` (1): DMA buffers, large allocations, shared data, GUI page contexts

### CubeMX Integration

**Critical**: Respect CubeMX `USER CODE BEGIN` / `USER CODE END` blocks. Code outside these blocks is **overwritten** when CubeMX regenerates. Both main firmware and BootLoader have independent CubeMX projects.

### Naming

- **Files**: Mixed case (`Player.c`, `Malloc.c`, `LCD.c`)
- **Functions**: `snake_case` for system/BSP, `CamelCase` for HAL, `nes_lowercase` for NES emulator
- **Structs**: `typedef struct Name { ... } Name_t;` — always `_t` suffix
- **Defines**: `UPPER_SNAKE_CASE`
- **GUI modules**: `GUI_<Feature>.c/h`, register `GUI_<Feature>_Register()`, show `GUI_<Feature>_Show()`
- **Media modules**: `Player_<Format>.c/h` or `Video_<Component>.c/h`
- **IAP shared protocol**: App and BootLoader share `IAP/IAP_Protocol.h` for flash layout, EEPROM slot defs, record struct macro `IAP_RECORD_MEMBERS`

### Debug and Utilities

- **Letter Shell v3.1.2**: UART1 command console (`Debug/shell.c`)
- **printf**: UART1 output (`MDK-ARM/UART1_Debug.c`), MicroLIB required, semihosting disabled
- **Perf commands**: `videostat` / `audiostat` for runtime AVI playback metrics
- **System errors**: `SYSTEM_STATUS_OK`, `RTOS_UNEXPECTION_FATAL`, `STACK_OVERFLOW`, `OUT_OF_HEAP_MEMORY`, etc. in `System_Status.h`
- **PC tools**: `Tools/hexdump` (hex viewer), `Tools/map_analyzer` (.map memory analyzer, C+Python+PyInstaller .exe)

## Common Pitfalls

1. **CCM ≠ DMA**: Never allocate DMA buffers from `SRAMCCM`. Always use `SRAMEX`.
2. **CubeMX USER CODE blocks**: All custom code must be inside them.
3. **Types**: Use `u8`/`u16`/`u32`, not `uint8_t`.
4. **RTOS API**: CMSIS-RTOS V2 only, not native FreeRTOS.
5. **External SRAM address**: 0x68000000, not 0x20000000.
6. **GBK encoding**: Source files use GBK for Chinese comments/strings.
7. **HardFault**: Auto-displayed on LCD (no serial/debugger needed).
8. **LCD interface**: ST7789 uses **SPI** (not FSMC parallel). Video Task takes exclusive SPI1/LCD ownership during playback.
9. **SDIO DMA alignment**: `ENABLE_SCRATCH_BUFFER` provides 512 B aligned buffer.
10. **GUI contexts**: Page-level `static` globals (~13 KB each) consume main SRAM. Prefer SRAMEX `mymalloc` for new pages.
11. **Video DMA collisions**: High-bitrate AVI playback may lose audio DMA events due to flag merging; monitor `collide/dma` stats. See `Docs/AVI高码率卡顿优化方案.md`.
12. **IAP records**: EEPROM dual-slot (SLOT0: 128, SLOT1: 168) uses sequence numbers for power-safe writes; verify `IAP_RECORD_SIZE` consistency before modifying `IAP_RECORD_MEMBERS`.
