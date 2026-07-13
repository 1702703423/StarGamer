# Star Gamer

STM32F407ZG 掌上游戏机 — NES 模拟器，集成 FreeRTOS、FATFS、LibJPEG、LVGL、LCD、I2S 音频。

## 项目

- **MCU**: STM32F407ZGT6（Cortex-M4F @ 168 MHz，1 MB Flash，192 KB 内部 SRAM + 1 MB 外部 SRAM）
- **屏幕**: ST7789，320×240，16-bit RGB565，SPI接口
- **工具链**: Keil MDK-ARM V5.32（ARMCLANG / AC6 编译器）
- **配置**: CubeMX 生成（`Star Gamer.ioc`），HAL 驱动层
- **RTOS**: FreeRTOS v10.3.1，CMSIS-RTOS V2 API，静态内存分配
- **入口**: `Core/Src/main.c` → `main()` → 初始化外设 → `osKernelStart()` → `defaultTask` → `init()`（`System/firmware_start.c`）

## 构建命令

- **编译**: 在 Keil uVision5 中打开 `MDK-ARM/Star Gamer.uvprojx`，按 **F7**（或 Project → Build Target）
- **烧录/调试**: **F8**（Flash → Download）使用 ST-Link/DAP-Link；调试会话按 **Ctrl+F5**
- **清理**: Project → Clean Targets

> 无 Makefile 或 CMake。构建完全在 Keil IDE 内管理。编译后自动从 ELF 生成 `.bin` 文件。

## 架构

| 模块 | 路径 | 职责 |
|------|------|------|
| **Core** | `Core/Src/`, `Core/Inc/` | `main.c`（入口、外设初始化），`freertos.c`（空闲/定时器任务栈分配），`stm32f4xx_it.c`（ISR 含 HardFault），HAL MSP/timebase |
| **Drivers** | `Drivers/` | CMSIS、STM32F4 HAL、`LCD/`（ST7789 SPI 显示驱动） |
| **System** | `System/` | `sys.h`（u8/u16/u32、位带宏 PAout/PAin），`Malloc`（CCM + 外部 SRAM 分配器），`System_Status`（系统状态/错误码/资源表/设置持久化），`firmware_start.c`（系统初始化 + HardFault LCD 显示），`adt_chain`（链表 ADT），`ccm_memory.h`（CCM 布局常量），`StarGamer_CCM.sct`（链接脚本） |
| **BSP** | `BSP/` | `w25q64`（SPI NOR Flash），`i2s`（I2S3 音频输出 + DMA），`at24c02`（I2C EEPROM），`key`（8 键驱动 + 游戏键位掩码别名） |
| **Middlewares** | `Middlewares/Third_Party/` | FreeRTOS、FatFs（SDIO，codepage 936）、LibJPEG、LVGL、**NES 模拟器**（6502 + PPU + APU + 240+ mapper） |
| **GUI** | `GUI/` | LVGL 页面管理器架构：`GUI_Manage`（页面生命周期/导航栈），`GUI_Main`（9 宫格主菜单），`GUI_NES`（NES 游戏选择），`GUI_Player`（音乐播放器），`GUI_Image`（图片查看器），`GUI_Settings`（系统设置），`GUI_FileManage`（可复用文件浏览器，支持扩展过滤/分页），`GUI_Font`（XBF 字库安装），`GUI_Task`（GUI 线程定义，4096 字节栈，`osPriorityAboveNormal`） |
| **FATFS / LIBJPEG** | `FATFS/`, `LIBJPEG/` | CubeMX 生成的中间件适配层（App + Target） |
| **Debug** | `Debug/` | Letter Shell v3.1.2（UART 命令控制台），`UART1_Debug.c`（printf 重定向），`shell_ext.c`（用户命令扩展），`fs_support/`（文件系统命令） |
| **Media** | `Media/` | `Player.c` — 音频播放编排（WAV 解析、I2S 喂数据） |
| **ELF Loader** | `ELF Loader/` | 动态 ELF 加载器（`dl_lib`），从 SD 卡运行时加载代码 |
| **IAP** | `IAP/` | 在应用编程桩（当前为空） |
| **APP** | `APP/` | 应用层（规划中，当前为空） |
| **EXTERN API** | `EXTERN API/` | 外部 API 接口（规划中，当前为空） |

### 启动流程

1. `main()` → HAL 初始化、时钟配置、外设初始化（GPIO/DMA/FSMC/SDIO/UART/DAC/SPI/I2S/RNG/TIM）
2. `osKernelInitialize()` → 分配 defaultTask 栈（CCM）→ `osKernelStart()`
3. `defaultTask` → `init()` → `System_init()`:
   - 初始化 CCM + 外部 SRAM 内存池
   - 初始化 Shell（若调试模式开启）
   - 创建帧缓冲资源表、任务资源表
   - 后续由各子系统注册线程和页面

### GUI 页面管理器

基于 `GUI_Manage.c/h` 的统一页面架构：

- **页面类型**（`Page_ID_t`）：`GUI_PAGE_MAIN_MENU`、`GUI_PAGE_NES`、`GUI_PAGE_ARDUBOY`、`GUI_PAGE_MUSIC`、`GUI_PAGE_IMAGE`、`GUI_PAGE_TEXT`、`GUI_PAGE_SETTINGS`、`GUI_PAGE_VIDEO`、`GUI_PAGE_AXF_LOADER`、`GUI_PAGE_FONT`
- **生命周期**：`Page_Create(init, callback)` → `Page_Show(id)` / `Page_Show_Anim()` → `Page_Back()` → `Page_Delete(id)`
- **导航**：内置页面历史栈，支持返回上一页
- **文件管理器**（`GUI_FileManage`）：可复用的 FatFs 文件浏览器组件，支持扩展过滤、OEM/UTF-8 转码、异步分页加载（`GUI_FILEMANAGE_MAX_ITEMS=32`）

### 内存架构

- **内部 SRAM**: 192 KB（128 KB 主 SRAM + 64 KB CCM）
  - CCM（0x10000000）：仅 CPU 可访问，**禁止 DMA**。由 `Malloc` 以 `SRAMCCM`（0）管理，64 KB 全用作堆（`ccm_memory.h`）
- **外部 SRAM**: 1 MB FSMC SRAM（0x68000000–0x680FFFFF）。由 `Malloc` 以 `SRAMEX`（1）管理（963 KB 可用，32 字节块）
- **帧缓冲**: 150 KB LCD 屏幕缓冲，从外部 SRAM 分配；第一缓冲区默认为 LVGL 渲染目标
- **分配器**: `mymalloc(memx, size)` / `myfree(memx, ptr)`，`memx` = `SRAMCCM`（0）或 `SRAMEX`（1）

### 关键子系统

- **LCD**: ST7789 240×320 RGB565，SPI 接口（内存映射写入），驱动 `Drivers/LCD/LCD.c`，背光 PWM（TIM12 CH2），方向 `USE_HORIZONTAL = 1`
- **音频**: I2S3（SPI3）+ DMA → 外部 DAC，22kHz 16-bit 全双工模式。`BSP/i2s.c` 提供统一「声卡」API（`audio_card_init/open/write`，双缓冲 DMA，回调注册）。NES APU 通过注册回调推送到 I2S
- **存储**: SD 卡 SDIO 4-bit + DMA（RX: DMA2_Stream3，TX: DMA2_Stream6）；W25Q64（8 MB）SPI Flash（SPI1）；AT24C02（256 B）I2C EEPROM
- **NES 模拟器**: 入口 `nes_main.c`，核心 `nes_ppu.c`/`nes_apu.c`/`nes_mapper.c`/`6502.s`，`NES_SKIP_FRAME = 2`（跳帧），mapper 文件 `000.cpp`–`255.cpp`
- **字库**: XBF 格式字库从 SD 卡加载到 SPI Flash（起始地址 0，2 MB 预留）。`myFont.c` 提供代理 `lv_font_t`，委托到 `GUI_Font_GetXbfFont()`。字库参数持久化在 `System_Settings_t` 中
- **HardFault 处理**: `firmware_start.c` 内嵌完整 HardFault Handler — 独立重新初始化 SPI1 + LCD GPIO，在 LCD 上绘制寄存器转储（R0-R3/R12/LR/PC/xPSR/CFSR/HFSR/MMFAR/BFAR），无需依赖 HAL 或 RTOS
- **按键**: 8 键驱动（`BSP/key.c`），`KEY_Id_t` 枚举 + 位掩码宏 `KEY_MASK_1`–`KEY_MASK_8`，预定义游戏键位别名（`KEY_GAME_UP/DOWN/LEFT/RIGHT/OK/A/B/SELECT/START`）

### LVGL 集成

- **LVGL 线程**: 独立线程 `lvgl_timer_task`，8192 字节栈，`osPriorityNormal`，每 10ms 调用 `lv_timer_handler()`
- **显示驱动**: `lv_port_disp_template.h`，使用外部 SRAM 帧缓冲
- **输入设备**: `lv_port_indev_init()` 注册按键输入

## 编码规范

- **类型**: 必须使用 `u8`/`u16`/`u32` 和 `s8`/`s16`/`s32`（定义在 `System/sys.h`），不要用 `uint8_t` 等标准类型
- **GPIO**: 位带宏 `PAout(n)`、`PBin(n)` 等（`sys.h`），用于快速单引脚 I/O
- **RTOS**: 使用 CMSIS-RTOS V2（`cmsis_os2.h`）——`osThreadNew`、`osMutexNew`、`osSemaphoreNew` 等，**不要**使用原生 FreeRTOS API。必须使用静态内存分配（预分配 TCB + 栈），通过 `sys_thread_stack_alloc()` 从 CCM 分配线程栈
- **CubeMX 安全区**: 所有自定义代码必须放在 `/* USER CODE BEGIN X */` / `/* USER CODE END X */` 之间，否则 CubeMX 重新生成时会丢失
- **printf**: UART1 输出（`Debug/UART1_Debug.c`），需启用 MicroLIB，禁用 semihosting
- **编码**: 源码包含中文注释（GBK 编码）；新增代码建议使用英文注释
- **命名**: 文件大小写混合（`Player.c`、`LCD.c`）；函数 `snake_case`（系统/BSP）、`CamelCase`（HAL 相关）、`nes_lowercase`（NES 模拟器）；结构体 `typedef struct Name { ... } Name_t;`；宏 `UPPER_SNAKE_CASE`
- **GUI 模块命名**: `GUI_<功能>.c/h`，注册函数 `GUI_<功能>_Register()`，显示函数 `GUI_<功能>_Show()`

## 常见陷阱

1. **CCM DMA 限制**: 绝不能从 `SRAMCCM` 分配 DMA 缓冲区——DMA 控制器无法访问 CCM。DMA 缓冲区始终使用 `SRAMEX`
2. **CubeMX 重新生成**: 始终将自定义代码放在 `USER CODE` 块内
3. **类型不匹配**: 使用 `u8`/`u16`/`u32`，而非标准 `uint8_t` 类型
4. **RTOS API**: 使用 CMSIS-RTOS V2（`cmsis_os2.h`），而非原生 FreeRTOS 调用
5. **外部 SRAM 地址**: 外部 SRAM 起始地址为 0x68000000，非 0x20000000（内部 SRAM）
6. **GBK 编码**: 编辑含中文注释/字符串的源文件需注意 GBK 编码，避免乱码
7. **HardFault 调试**: HardFault 信息会自动显示在 LCD 上（320×240，5×7 字体），无需串口或调试器即可查看寄存器状态

## 备注

- 根目录 `myFont.c` + `myFont.bin`（1 MB）为运行时字库代理和默认字库二进制
- `reasonix.toml` 为 Reasonix 权限配置文件，记录允许的 shell 命令
- `.agents/skills/arm-cortex-expert/` 为 ARM Cortex-M 专家技能，可被 AI 会话调用
- `APP/` 和 `EXTERN API/` 目录为规划中的架构扩展点
- `GUI_Axf.c/h` 和 `GUI_Text.c/h` 为占位文件（0 字节），对应 AXF 加载器和文本查看器页面
