# Star Gamer

STM32F407ZG 掌上游戏机 — NES 模拟器，集成 FreeRTOS、FATFS、LibJPEG、LCD、I2S 音频。

## 项目

- **MCU**: STM32F407ZGT6（Cortex-M4F @ 168 MHz，1 MB Flash，192 KB 内部 SRAM + 1 MB 外部 SRAM）
- **屏幕**: ST7789，320×240，16-bit RGB565，FSMC 并行接口
- **工具链**: Keil MDK-ARM V5.32（ARMCLANG / AC6 编译器）
- **配置**: CubeMX 生成（`Star Gamer.ioc`），HAL 驱动层
- **RTOS**: FreeRTOS v10.3.1，CMSIS-RTOS V2 API
- **入口**: `Core/Src/main.c` → `main()` → 初始化外设 → `osKernelStart()`

## 构建命令

- **编译**: 在 Keil uVision5 中打开 `MDK-ARM/Star Gamer.uvprojx`，按 **F7**（或 Project → Build Target）
- **烧录/调试**: **F8**（Flash → Download）使用 ST-Link/DAP-Link；调试会话按 **Ctrl+F5**
- **清理**: Project → Clean Targets

> 无 Makefile 或 CMake。构建完全在 Keil IDE 内管理。编译后自动从 ELF 生成 `.bin` 文件。

## 架构

| 模块 | 路径 | 职责 |
|------|------|------|
| **Core** | `Core/Src/`, `Core/Inc/` | `main.c`（入口、外设初始化），`freertos.c`（defaultTask），`stm32f4xx_it.c`（ISR），HAL MSP/timebase |
| **Drivers** | `Drivers/` | CMSIS、STM32F4 HAL、`LCD/`（ST7789 FSMC 显示驱动） |
| **System** | `System/` | `sys.h`（全局类型定义 u8/u16/u32、位带宏 PAout/PAin 等），`Malloc`（多内存池分配器：CCM + 外部 SRAM），`System_Status`（系统状态/错误码/资源表），`adt_chain`（链表 ADT） |
| **BSP** | `BSP/` | `w25q64`（SPI NOR Flash），`i2s`（I2S3 音频输出 + DMA），`at24c02`（I2C EEPROM），`key`（按键驱动） |
| **Middlewares** | `Middlewares/Third_Party/` | FreeRTOS、FatFs（SDIO，codepage 936）、LibJPEG、LVGL、**NES 模拟器**（6502 + PPU + APU + 240+ mapper） |
| **GUI** | `GUI/` | `GUI_Task.c` — LVGL RTOS 线程（`osPriorityAboveNormal`，2048 字节栈），头文件 `GUI/GUI_Task.h` |
| **FATFS / LIBJPEG** | `FATFS/`, `LIBJPEG/` | CubeMX 生成的中间件适配层（App + Target） |
| **Debug** | `Debug/` | Letter Shell v3.1.2（UART 命令控制台），UART1 printf 重定向，测试框架 |
| **Media** | `Media/` | `Player.c` — 音频播放编排（WAV 解析、I2S 喂数据） |
| **ELF Loader** | `ELF Loader/` | 动态 ELF 加载器（`dl_lib`），从 SD 卡运行时加载代码 |
| **IAP** | `IAP/` | 在应用编程桩（当前为空） |

### 内存架构

- **内部 SRAM**: 192 KB（128 KB 主 SRAM + 64 KB CCM）
  - CCM（0x10000000）：仅 CPU 可访问，**禁止 DMA**。由 `Malloc` 以 `SRAMCCM`（0）管理
- **外部 SRAM**: 1 MB FSMC SRAM（0x68000000–0x680FFFFF）。由 `Malloc` 以 `SRAMEX`（1）管理（963 KB 可用，32 字节块）
- **帧缓冲**: 150 KB LCD 屏幕缓冲，从外部 SRAM 分配
- **分配器**: `mymalloc(memx, size)` / `myfree(memx, ptr)`，`memx` = `SRAMCCM`（0）或 `SRAMEX`（1）

### 关键子系统

- **LCD**: ST7789 240×320 RGB565，FSMC 并行接口（内存映射写入），驱动 `Drivers/LCD/LCD.c`，背光 PWM（TIM12 CH2），方向 `USE_HORIZONTAL = 1`
- **音频**: I2S3（SPI3）+ DMA → 外部 DAC。`BSP/i2s.c` 提供统一「声卡」API（`audio_card_init/open/write`，双缓冲 DMA，回调注册）。NES APU 通过注册回调推送到 I2S
- **存储**: SD 卡 SDIO 4-bit + DMA；W25Q64（8 MB）SPI Flash；AT24C02（256 B）I2C EEPROM
- **NES 模拟器**: 入口 `nes_main.c`，核心 `nes_ppu.c`/`nes_apu.c`/`nes_mapper.c`/`6502.s`，`NES_SKIP_FRAME = 2`（跳帧），mapper 文件 `000.cpp`–`255.cpp`

## 编码规范

- **类型**: 必须使用 `u8`/`u16`/`u32` 和 `s8`/`s16`/`s32`（定义在 `System/sys.h`），不要用 `uint8_t` 等标准类型
- **GPIO**: 位带宏 `PAout(n)`、`PBin(n)` 等（`sys.h`），用于快速单引脚 I/O
- **RTOS**: 使用 CMSIS-RTOS V2（`cmsis_os2.h`）——`osThreadNew`、`osMutexNew`、`osSemaphoreNew` 等，**不要**使用原生 FreeRTOS API。优先静态分配（预分配栈和控制块）
- **CubeMX 安全区**: 所有自定义代码必须放在 `/* USER CODE BEGIN X */` / `/* USER CODE END X */` 之间，否则 CubeMX 重新生成时会丢失
- **printf**: UART1 输出（`Debug/UART1_Debug.c`），需启用 MicroLIB，禁用 semihosting
- **编码**: 源码包含中文注释（GBK 编码）；新增代码建议使用英文注释
- **命名**: 文件大小写混合（`Player.c`、`LCD.c`）；函数 `snake_case`（系统/BSP）、`CamelCase`（HAL 相关）、`nes_lowercase`（NES 模拟器）；结构体 `typedef struct Name { ... } Name_t;`；宏 `UPPER_SNAKE_CASE`

## 常见陷阱

1. **CCM DMA 限制**: 绝不能从 `SRAMCCM` 分配 DMA 缓冲区——DMA 控制器无法访问 CCM。DMA 缓冲区始终使用 `SRAMEX`
2. **CubeMX 重新生成**: 始终将自定义代码放在 `USER CODE` 块内
3. **类型不匹配**: 使用 `u8`/`u16`/`u32`，而非标准 `uint8_t` 类型
4. **RTOS API**: 使用 CMSIS-RTOS V2（`cmsis_os2.h`），而非原生 FreeRTOS 调用
5. **外部 SRAM 地址**: 外部 SRAM 起始地址为 0x68000000，非 0x20000000（内部 SRAM）

## 备注

-
