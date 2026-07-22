# Star Gamer

STM32F407ZG 掌上游戏机 — NES 模拟器 / Arduboy 游戏 / AVI 视频播放 / MP3 音乐播放，集成 FreeRTOS、LVGL、FatFs、LibJPEG、Helix MP3、I2S 音频、USB MSC。

## 项目

- **MCU**: STM32F407ZGT6（Cortex-M4F @ 168 MHz，1 MB Flash，192 KB 内部 SRAM + 1 MB 外部 SRAM）
- **屏幕**: ST7789，320×240，16-bit RGB565，SPI 接口
- **工具链**: Keil MDK-ARM V5.32（ARMCLANG / AC6 编译器）
- **配置**: 双 CubeMX 工程 — 主固件 `Star Gamer.ioc` + BootLoader `BootLoader/StarGamerBootLoader/StarGamerBootLoader.ioc`
- **RTOS**: FreeRTOS v10.3.1，CMSIS-RTOS V2 API，静态内存分配
- **入口**: `Core/Src/main.c` → `main()` → 外设初始化 → `osKernelStart()` → `defaultTask` → `init()`（`System/firmware_start.c`）

## 构建命令

- **主固件编译**: 在 Keil uVision5 中打开 `MDK-ARM/Star Gamer.uvprojx`，按 **F7**
- **BootLoader 编译**: 打开 `BootLoader/StarGamerBootLoader/MDK-ARM/StarGamerBootLoader.uvprojx`，按 **F7**
- **烧录/调试**: **F8**（Flash → Download）使用 ST-Link/DAP-Link；调试会话按 **Ctrl+F5**
- **清理**: Project → Clean Targets

> 无 Makefile 或 CMake。构建完全在 Keil IDE 内管理。编译后自动从 ELF 生成 `.bin` 文件。

## 架构

| 模块 | 路径 | 职责 |
|------|------|------|
| **Core** | `Core/Src/`, `Core/Inc/` | `main.c`（入口、外设初始化），`freertos.c`（空闲/定时器任务），`stm32f4xx_it.c`（ISR），HAL MSP/timebase |
| **Drivers** | `Drivers/` | CMSIS、STM32F4 HAL、`LCD/`（ST7789 SPI 显示驱动，含 RGB565 块传输/缩放裁剪） |
| **System** | `System/` | `sys.h`（u8/u16/u32、位带宏 PAout/PAin），`Malloc`（CCM + SRAMEX 双堆分配器），`System_Status`（系统状态/错误码/资源表/设置持久化），`firmware_start.c`（系统初始化 + HardFault LCD 显示），`adt_chain`（链表），`kfifo`（内核 FIFO，用于 PCM 音频缓冲），`ccm_memory.h`、`StarGamer_CCM.sct`（链接脚本） |
| **BSP** | `BSP/` | `w25q64`（SPI NOR Flash），`i2s`（I2S3 音频输出 + DMA，增强 API：`audio_card_open_prepare`），`at24c02`（I2C EEPROM），`key`（8 键驱动 + 游戏键位别名） |
| **Middlewares** | `Middlewares/` | FreeRTOS、FatFs（SDIO，codepage 936）、LibJPEG（自定义 `jpeg_memory_provider`）、LVGL、NES 模拟器、Arduboy 兼容层、**Helix MP3 解码库**（ARM Cortex-M4 定点优化） |
| **GUI** | `GUI/` | LVGL 页面管理器架构（见下方 GUI 章节） |
| **Media** | `Media/` | 播放器子系统（见下方 Media 章节） |
| **BootLoader** | `BootLoader/StarGamerBootLoader/` | 独立 CubeMX 工程，64 KB 裸机固件更新器（Flash 扇区 0–3），无 RTOS/FATFS/LVGL |
| **USB_DEVICE** | `USB_DEVICE/` | USB MSC 设备栈（`App/` + `Target/`），SD 卡模拟 U 盘 |
| **Debug** | `Debug/` | Letter Shell v3.1.2（UART 控制台），`shell_ext.c`（用户命令），`fs_support/`（文件系统命令），`shell_companion.c` |
| **ELF Loader** | `ELF Loader/` | 动态 ELF 加载器（`dl_lib`），从 SD 卡运行时加载 `.axf` 代码 |
| **APP** | `APP/Arduboy/` | Arduboy 游戏运行时（`Arduboy_Runtime`、`Arduboy_Port`、`Arduboy_Games`） |
| **FATFS / LIBJPEG** | `FATFS/`, `LIBJPEG/` | CubeMX 生成的中间件适配层（App + Target） |
| **IAP** | `IAP/` | 在应用编程桩（待实现） |
| **EXTERN API** | `EXTERN API/` | 外部 API 接口（待实现） |
| **Docs** | `Docs/` | 架构设计文档：AVI 视频播放器、BootLoader 设计、Arduboy 移植、Helix MP3 移植、CCM 栈优化、GUI SRAM 优化、AVI 优化方案 |
| **Tools** | `Tools/` | PC 端开发工具：`hexdump`（文件十六进制查看，C + Python）、`map_analyzer`（ARMCLANG .map 内存分析，C + Python tkinter GUI） |
| **Tests** | `Tests/AVI_Parser_Host/` | AVI 解析器 PC 端回归测试 |

### 启动流程

1. `main()` → HAL 初始化、时钟配置（HSE 8MHz → PLL 168MHz）、外设初始化（CRC/GPIO/DMA/FSMC/SDIO/UART/DAC/SPI/I2S/RNG/TIM）
2. `osKernelInitialize()` → 从 CCM 分配 defaultTask 栈（4096 B）→ `osKernelStart()`
3. `defaultTask` → `init()`（`System/firmware_start.c`）:
   - 初始化 SRAMCCM + SRAMEX 内存池
   - 挂载 SD 卡 FatFs
   - 初始化 Shell（若调试模式）
   - 显示启动动画（`GUI_Start_Show`）
   - 创建帧缓冲资源表、任务资源表
   - 各子系统注册线程和页面

### GUI 页面管理器

基于 `GUI_Manage.c/h` 的统一页面架构：

| 页面 ID | 文件 | 功能 |
|---------|------|------|
| `GUI_PAGE_MAIN_MENU` | `GUI_Main.c` | 9 宫格主菜单（NES / Arduboy / 音乐 / 图片 / 文本 / 视频 / 设置 / 字体） |
| `GUI_PAGE_NES` | `GUI_NES.c` | NES 游戏选择与启动 |
| `GUI_PAGE_ARDUBOY` | `GUI_Arduboy.c` | Arduboy 游戏列表（Buttons / Dino / FlappyBall） |
| `GUI_PAGE_MUSIC` | `GUI_Player.c` | 音乐播放器（WAV / MP3） |
| `GUI_PAGE_IMAGE` | `GUI_Image.c` | JPEG 图片查看器 |
| `GUI_PAGE_TEXT` | `GUI_Text.c` | 文本查看器（UTF-8 / GBK，缺字回退 `口`） |
| `GUI_PAGE_SETTINGS` | `GUI_Settings.c` | 系统设置 |
| `GUI_PAGE_VIDEO` | `GUI_Video.c` | AVI 视频播放器（MJPEG + PCM） |
| `GUI_PAGE_FONT` | `GUI_Font.c` | XBF 字库安装 |
| `GUI_PAGE_AXF_LOADER` | `GUI_Axf.c` | AXF 加载器（占位） |

- **起始页**: `GUI_Start.c` — 启动动画（spinner + 状态文字），初始化完成后回调进入主菜单
- **文件浏览器**: `GUI_FileManage.c/h` — 可复用的 FatFs 文件浏览器组件，支持扩展过滤、OEM/UTF-8 转码、异步分页加载（`GUI_FILEMANAGE_MAX_ITEMS=32`）
- **生命周期**: `Page_Create(init, callback)` → `Page_Show(id)` / `Page_Show_Anim()` → `Page_Back()` → `Page_Delete(id)`
- **导航**: 内置页面历史栈，支持返回上一页
- **GUI 线程**: `GUI_Task.c`，4096 字节栈，`osPriorityAboveNormal`

### Media 播放器子系统

| 模块 | 文件 | 职责 |
|------|------|------|
| **播放器核心** | `Player.c/h` | 播放器编排、编解码器注册、音频数据管道 |
| **编解码器接口** | `Player_Codec.h` | 抽象 codec 接口：`Player_Codec_Read_t` / `Player_Codec_Close_t` |
| **WAV 解码** | `Player_WAV.c/h` | PCM WAV 文件解析与解码 |
| **MP3 解码** | `Player_MP3.c/h` | MP3 解码器包装，对接 Helix 库 |
| **视频框架** | `Video.c/h` | AVI 播放状态机、音视频同步（PCM 时钟驱动） |
| **AVI 解析** | `AVI_Parser.c/h` | RIFF/AVI 容器解析，小端读取，流索引 |
| **MJPEG 解码** | `Video_MJPEG.c/h` | MJPEG 帧解码管线，LibJPEG 流式输入 |
| **PCM 音频** | `Video_PCM.c/h` | AVI PCM 音频流解码，kfifo 缓冲，I2S DMA 喂数 |
| **视频内存** | `Video_Memory.c/h` | 视频帧缓冲池管理（SRAMEX），条带缓冲 |

### 内存架构

- **内部 SRAM**: 192 KB（128 KB 主 SRAM + 64 KB CCM）
  - 主 SRAM（0x20000000）：`.data/.bss`、FreeRTOS `ucHeap`、静态全局对象。GUI 页面上下文逐步迁移到 SRAMEX
  - CCM（0x10000000）：仅 CPU 可访问，**禁止 DMA**。由 `Malloc` 以 `SRAMCCM`（0）管理，64 KB 全用作堆（`ccm_memory.h`），主要用于 RTOS 任务栈分配
- **外部 SRAM**: 1 MB FSMC SRAM（0x68000000–0x680FFFFF）。由 `Malloc` 以 `SRAMEX`（1）管理（963 KB 可用，32 字节块）。LVGL 堆、帧缓冲、GUI 页面上下文、视频缓冲、PCM FIFO 均走 SRAMEX
- **分配器**: `mymalloc(memx, size)` / `myfree(memx, ptr)`，`memx` = `SRAMCCM`（0）或 `SRAMEX`（1）。已移除废弃的 `SRAMMAIN` 测试堆，`SRAMBANK` 固定为 2
- **帧缓冲**: 150 KB LCD 屏幕缓冲，从外部 SRAM 分配；第一缓冲区默认为 LVGL 渲染目标
- **视频内存池**: 80 KB 片上 SRAM 对齐池（0x2000C000–0x2001FFFF）用于 Video Task 栈、FatFs/JPEG 对象、LCD DMA 条带；32 KB PCM FIFO + 4 KB PCM I/O 缓冲 + I2S DMA 双缓冲走 SRAMEX
- **线程栈回收**: 临时线程（图片解码、NES、Arduboy）支持栈回收，通过 `sys_thread_stack_free()` 在父上下文释放

### 关键子系统

- **LCD**: ST7789 240×320 RGB565，SPI 接口（DMA 写入）。驱动 `Drivers/LCD/LCD.c`，扩展 RGB565 块传输 / 缩放裁剪接口。背光 PWM（TIM12 CH2），方向 `USE_HORIZONTAL = 1`
- **音频**: I2S3（SPI3）+ DMA → 外部 DAC。`BSP/i2s.c` 提供「声卡」API：
  - `audio_card_init()` / `audio_card_open_prepare()` / `audio_card_open()` / `audio_card_write()`
  - 双缓冲 DMA，回调注册（`audio_card_register_tx()`）
  - NES APU、MP3/WAV 播放器、AVI PCM 均通过回调推送到 I2S
- **存储**:
  - SD 卡 SDIO 4-bit + DMA（RX: DMA2_Stream3，TX: DMA2_Stream6），FatFs codepage 936，已启用 512 B 对齐 scratch buffer
  - W25Q64（8 MB）SPI Flash（SPI1）：分区为字体区 2 MB + 预留区 4 MB + 固件暂存区 1 MB + 固件备份区 1 MB
  - AT24C02（256 B）I2C EEPROM
- **NES 模拟器**: 入口 `nes_main.c`，核心 `nes_ppu.c`/`nes_apu.c`/`nes_mapper.c`/`6502.s`，`NES_SKIP_FRAME = 2`（跳帧），mapper 文件 `000.cpp`–`255.cpp`
- **Arduboy 游戏**: `Middlewares/Third_Party/Arduboy/` 含兼容层（`src/`）+ 3 款游戏（Buttons / Dino / FlappyBall）。128×64 1bpp framebuffer，通过 `APP/Arduboy/Arduboy_Runtime.c` 管理生命周期，`Arduboy_Port.c` 对接 LCD 显示和按键
- **Helix MP3**: `Middlewares/libhelix/` 定点 Layer III 解码器，14 个 C 文件 + 2 个 ARM 汇编优化（`asmmisc_rvds.s`、`asmpoly_thumb2_rvds.s`），通过 `Player_MP3.c` 和 `helix_support.c` 适配
- **USB MSC**: `USB_DEVICE/`，SD 卡模拟 U 盘，通过 CubeMX USB OTG FS 驱动实现
- **字库**: XBF 格式字库从 SD 卡加载到 SPI Flash（起始地址 0，2 MB 预留）。`myFont.c` 提供代理 `lv_font_t`。字库参数持久化在 `System_Settings_t`
- **HardFault 处理**: `firmware_start.c` 内嵌完整 HardFault Handler — 独立重新初始化 SPI1 + LCD GPIO，在 LCD 上绘制寄存器转储（R0-R3/R12/LR/PC/xPSR/CFSR/HFSR/MMFAR/BFAR），无需依赖 HAL 或 RTOS
- **按键**: 8 键驱动（`BSP/key.c`），`KEY_Id_t` 枚举 + 位掩码宏，预定义游戏键位别名（`KEY_GAME_UP/DOWN/LEFT/RIGHT/OK/A/B/SELECT/START`）

### BootLoader

独立 CubeMX 工程，位于 `BootLoader/StarGamerBootLoader/`，设计文档见 `Docs/BootLoader-Design.md`：

- **Flash 分区**: BootLoader 64 KB（扇区 0–3，0x08000000–0x0800FFFF），Application 960 KB（扇区 4–11，0x08010000–0x080FFFFF）
- **更新流程**: SD 卡 `update.bin` → 写入 W25Q64 暂存区（0x600000）→ CRC32 校验 → 擦写内部 Flash → 校验后跳转 App
- **安全机制**: 双备份（W25Q64 备份区 0x700000）、断电容错、CRC 全程校验
- **外设**: 仅依赖 SPI1（LCD / W25Q64 / 按键）、内部 Flash HAL、CRC

### LVGL 集成

- **LVGL 线程**: 独立线程 `lvgl_timer_task`，8192 字节栈，`osPriorityNormal`，每 10ms 调用 `lv_timer_handler()`
- **显示驱动**: `lv_port_disp_template.h`，使用外部 SRAM 帧缓冲
- **输入设备**: `lv_port_indev_init()` 注册按键输入
- **视频播放期间**: 通过 `lvgl_stop_ui()` / `lvgl_start_ui()` 暂停/恢复 LVGL，释放 LCD/SPI1 所有权给 Video Task

## 编码规范

- **类型**: 必须使用 `u8`/`u16`/`u32` 和 `s8`/`s16`/`s32`（定义在 `System/sys.h`），不要用 `uint8_t` 等标准类型
- **GPIO**: 位带宏 `PAout(n)`、`PBin(n)` 等（`sys.h`），用于快速单引脚 I/O
- **RTOS**: 使用 CMSIS-RTOS V2（`cmsis_os2.h`）——`osThreadNew`、`osMutexNew`、`osSemaphoreNew` 等，**不要**使用原生 FreeRTOS API。必须使用静态内存分配（预分配 TCB + 栈），通过 `sys_thread_stack_alloc()` 从 CCM 分配线程栈。临时线程退出后通过 `sys_thread_stack_free()` 回收栈
- **CubeMX 安全区**: 所有自定义代码必须放在 `/* USER CODE BEGIN X */` / `/* USER CODE END X */` 之间，否则 CubeMX 重新生成时会丢失
- **printf**: UART1 输出（`MDK-ARM/UART1_Debug.c`），需启用 MicroLIB，禁用 semihosting
- **编码**: 源码包含中文注释（GBK 编码）；新增代码建议使用英文注释
- **命名**: 文件大小写混合（`Player.c`、`LCD.c`）；函数 `snake_case`（系统/BSP）、`CamelCase`（HAL 相关）、`nes_lowercase`（NES 模拟器）；结构体 `typedef struct Name { ... } Name_t;`；宏 `UPPER_SNAKE_CASE`
- **GUI 模块命名**: `GUI_<功能>.c/h`，注册函数 `GUI_<功能>_Register()`，显示函数 `GUI_<功能>_Show()`
- **Media 模块命名**: `Player_<格式>.c/h` 或 `Video_<组件>.c/h`
- **内存池选择**: DMA 缓冲区 **必须**走 `SRAMEX`；CPU-only 数据（线程栈、局部缓冲）走 `SRAMCCM` 或主 SRAM

## 常见陷阱

1. **CCM DMA 限制**: 绝不能从 `SRAMCCM` 分配 DMA 缓冲区——DMA 控制器无法访问 CCM。DMA 缓冲区始终使用 `SRAMEX`
2. **CubeMX 重新生成**: 始终将自定义代码放在 `USER CODE` 块内。主固件和 BootLoader 各有独立的 CubeMX 工程
3. **类型不匹配**: 使用 `u8`/`u16`/`u32`，而非标准 `uint8_t` 类型
4. **RTOS API**: 使用 CMSIS-RTOS V2（`cmsis_os2.h`），而非原生 FreeRTOS 调用
5. **外部 SRAM 地址**: 外部 SRAM 起始地址为 0x68000000，非 0x20000000（内部 SRAM）
6. **GBK 编码**: 编辑含中文注释/字符串的源文件需注意 GBK 编码，避免乱码
7. **HardFault 调试**: HardFault 信息会自动显示在 LCD 上（320×240，5×7 字体），无需串口或调试器即可查看寄存器状态
8. **LCD 接口**: 本工程 ST7789 走 **SPI** 接口（非 FSMC 并行）。视频播放期间 Video Task 独占 SPI1/LCD
9. **SDIO DMA 对齐**: FatFs 通过 `ENABLE_SCRATCH_BUFFER` 提供 512 B 对齐缓冲区，避免 DMA word 对齐覆盖压缩流边界
10. **GUI 页面上下文**: 页面级 `static` 全局变量（内嵌 `GUI_FileManage_t`）占用大量主 SRAM（单个约 13 KB），新增页面应优先从 SRAMEX 动态分配

## 备注

- 根目录 `myFont.c` + `myFont.bin`（1 MB）为运行时字库代理和默认字库二进制
- `reasonix.toml` 为 Reasonix 权限配置文件
- `.agents/skills/arm-cortex-expert/` 为 ARM Cortex-M 专家技能
- `BootLoader/StarGamerBootLoader/` 为独立 CubeMX 工程，与主固件共享 Drivers/BSP 部分代码
- `GUI_Axf.c/h`（AXF 加载器页面）为占位文件（0 字节）
- `IAP/`、`EXTERN API/` 为规划中的架构扩展点
- `Docs/` 目录含详细设计文档，修改前应查阅相关文档
