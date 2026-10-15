# Star Gamer

STM32F407ZG 掌上游戏机 — NES 模拟器 / Arduboy 游戏 / AVI 视频播放 / MP3 音乐播放，集成 FreeRTOS、LVGL、FatFs、LibJPEG、Helix MP3、I2S 音频、USB MSC。

## 项目

- **MCU**: STM32F407ZGT6（Cortex-M4F，HSE 8 MHz 超频至 192/216/240 MHz 三档可切换，默认 240 MHz；1 MB Flash，192 KB 内部 SRAM + 1 MB 外部 SRAM）
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
| **System** | `System/` | `sys.h`（u8/u16/u32、位带宏 PAout/PAin），`Malloc`（CCM + SRAMEX 双堆分配器，`sys_malloc`/`sys_free`/`sys_malloc_aligned`），`System_Status`（系统状态/错误码/资源表/设置持久化），`firmware_start.c`（系统初始化 + HardFault LCD 显示），`Memory_Dump`（HardFault 内存转储到 W25Q64 / SD 卡），`adt_chain`（链表），`kfifo`（内核 FIFO，Linux 风格泛型队列，用于 PCM 音频缓冲），`ccm_memory.h`、`StarGamer_CCM.sct`（链接脚本，1 个 Flash 执行区 + 2 个 SRAM 执行区） |
| **BSP** | `BSP/` | `w25q64`（SPI NOR Flash），`i2s`（I2S3 音频输出 + DMA，增强 API：`audio_card_open_prepare`，含碰撞统计），`at24c02`（I2C EEPROM），`key`（8 键驱动 + 游戏键位别名） |
| **Middlewares** | `Middlewares/` | FreeRTOS、FatFs（SDIO，codepage 936）、LibJPEG（自定义 `jpeg_memory_provider`）、LVGL、NES 模拟器、Arduboy 兼容层、BMP 图像库（占位）、**Helix MP3 解码库**（ARM Cortex-M4 定点优化，24 个源文件含 ARM 汇编） |
| **GUI** | `GUI/` | LVGL 页面管理器架构（见下方 GUI 章节） |
| **Media** | `Media/` | 播放器子系统（见下方 Media 章节） |
| **BootLoader** | `BootLoader/StarGamerBootLoader/` | 独立 CubeMX 工程，64 KB 裸机固件更新器（Flash 扇区 0–3），`Boot/` 模块化分层（`bootloader` 编排 / `boot_crc` CRC32 / `boot_storage` 存储 / `boot_ui` UI / `boot_platform` 平台定义），已编译生成 .hex/.bin/.axf，无 RTOS/FATFS/LVGL |
| **USB_DEVICE** | `USB_DEVICE/` | USB MSC 设备栈（`App/` + `Target/`），SD 卡模拟 U 盘 |
| **Debug** | `Debug/` | Letter Shell v3.1.2（UART 控制台），`shell_ext.c`（用户命令），`shell_support/`（Shell 基础设施：mutex/UART 读写/空闲钩子/System_init），`fs_support/`（文件系统命令 cd/ls），`shell_companion.c`，`videostat`/`audiostat` 性能统计命令，`UART1_Debug.c`（printf 重定向） |
| **ELF Loader** | `ELF Loader/` | 动态 ELF 加载器（`dl_lib`、`dl_elf.h`、`dl_arch.c`、`dl_vector.c`），`common/`（C 标准库 SVC shim），从 SD 卡运行时加载 `.axf` 可重定位代码，通过 SVC 调用共享宿主函数 |
| **APP** | `APP/Arduboy/` | Arduboy 游戏运行时（`Arduboy_Runtime` — 状态机管理、`Arduboy_Port` — 硬件移植层、`Arduboy_Games` — 游戏注册表） |
| **FATFS / LIBJPEG** | `FATFS/`, `LIBJPEG/` | CubeMX 生成的中间件适配层（App + Target） |
| **IAP** | `IAP/` | 在应用编程模块（CRC32 校验、EEPROM 双槽容错记录、W25Q64 暂存/备份区管理、中断向量验证、进度回调） |
| **EXTERN API** | `EXTERN API/` | AXF 动态加载器宿主端 API（`Extern_API.c/h` — LCD 控制/按键/延时 SVC 服务，`stargamer_axf_api.h` — AXF 应用侧头文件，含 `README.md` 使用文档） |
| **PCB** | `PCB/` | 立创 EDA 硬件设计工程文件（`ProPrj_StarGamer_*.epro2`） |
| **Docs** | `Docs/` | 架构设计文档：AVI 视频播放器（完整设计 + 撕裂分析 + 高码率卡顿优化方案 + 日志优化点）、BootLoader 设计、Arduboy 移植、Helix MP3 移植、CCM 栈优化、GUI SRAM 优化、SRAMEX 优化、内存规划重构、用户手册 |
| **Star Gamer ELF** | `Star Gamer ELF/` | 独立 Keil ARMCLANG 工程，构建 AXF 可重定位 demo 应用（`axf_demo.c` — 图形绘制/按键计数器，`dl_User_api.h` — 用户 API 编号），产物 `Star Gamer ELF.axf` 放到 SD 卡根目录后由 AXF Loader 页面加载运行 |
| **Star Gamer ELF - 2048** | `Star Gamer ELF - 2048/` | 独立 Keil 工程，构建 AXF 2048 游戏应用（`game_2048.c` — 320×240 RGB565 帧缓冲、方向键滑动、PB7 退出），产物 `Star Gamer ELF - 2048.axf` 由 AXF Loader 加载运行 |
| **Tools** | `Tools/` | PC 端开发工具：`hexdump`（文件十六进制查看，C + Python：CLI 与 tkinter GUI 双前端，共用同一转储内核 + PyInstaller 打包 .exe）、`map_analyzer`（ARMCLANG .map 内存分析，C + Python tkinter GUI + PyInstaller 打包 .exe） |
| **Tests** | `Tests/AVI_Parser_Host/`、`Tests/AXF_Loader_Host/`、`Tests/SDIO_Config_Host/` | AVI 解析器 PC 端回归测试（含边界条件单元测试；AVI 样本需自备，不入库）；AXF 加载器主机端验证脚本 `validate_axf.py`；SDIO IRQ 优先级验证脚本（验证 DMA 流中断优先级高于 SDIO 中断） |

### 启动流程

1. `main()` → HAL 初始化、时钟配置（HSE 8MHz → PLL 192/216/240 MHz，PLLM=4/PLLP=2，默认 240 MHz）、外设初始化（CRC/GPIO/DMA/FSMC/SDIO/UART/DAC/SPI/I2S/RNG/TIM）
2. `osKernelInitialize()` → 从 CCM 分配 defaultTask 栈（4096 B）→ `osKernelStart()`
3. `defaultTask` → `init()`（`System/firmware_start.c`）:
   - `System_init()`：初始化 SRAMCCM + SRAMEX 内存池、加载系统设置（`System_Settings_Init`）、初始化 Shell（若调试模式）、创建帧缓冲/任务资源表、`creat_frame_buffer`
   - `mount_fs()` / `bsp_init()`：挂载 SD 卡 FatFs、初始化按键 / W25Q64
   - 有待导出 HardFault 转储时调用 `Memory_Dump_ExportPending()`
   - `USB_DEVICE_MSC_ApplySetting()`
   - `lvgl_start_task()`：启动 LVGL 任务 → `GUI_Start_Show(GUI_Init)` 启动动画 → 各页面注册

### GUI 页面管理器

基于 `GUI_Manage.c/h` 的统一页面架构：

| 页面 ID | 文件 | 功能 |
|---------|------|------|
| `GUI_PAGE_MAIN_MENU` | `GUI_Main.c` | 9 宫格主菜单（NES / Arduboy / 音乐 / 图片 / 文本 / 视频 / 设置 / 字体） |
| `GUI_PAGE_NES` | `GUI_NES.c` | NES 游戏选择与启动 |
| `GUI_PAGE_ARDUBOY` | `GUI_Arduboy.c` | Arduboy 游戏列表（Buttons / Dino / FlappyBall） |
| `GUI_PAGE_MUSIC` | `GUI_Player.c` | 音乐播放器（WAV / MP3，含 Seek 进度交互） |
| `GUI_PAGE_IMAGE` | `GUI_Image.c` | JPEG 图片查看器 |
| `GUI_PAGE_TEXT` | `GUI_Text.c` | 文本查看器（UTF-8 / GBK，缺字回退 `口`，放宽浏览限制） |
| `GUI_PAGE_SETTINGS` | `GUI_Settings.c` | 系统设置 |
| `GUI_PAGE_VIDEO` | `GUI_Video.c` | AVI 视频播放器（MJPEG + PCM，含 Seek） |
| `GUI_PAGE_FONT` | `GUI_Font.c` | XBF 字库安装与卸载 |
| `GUI_PAGE_AXF_LOADER` | `GUI_Axf.c` | AXF 动态加载器（EL 加载器宿主页面，含状态机、文件选择、运行监控、栈回收） |

- **主题系统**: `GUI_Theme.c/h` — 深色/浅色主题切换，定义 20+ 颜色 token（`GUI_ThemeColor_t`），统一管理 GUI 配色

- **起始页**: `GUI_Start.c` — 启动动画（spinner + 状态文字），初始化完成后回调进入主菜单，支持 ready 回调
- **文件浏览器**: `GUI_FileManage.c/h` — 可复用的 FatFs 文件浏览器组件，支持扩展过滤、OEM/UTF-8 转码、异步分页加载（`GUI_FILEMANAGE_MAX_ITEMS=32`），More/Prev 双向翻页（Prev 基于页基址栈 `GUI_FILEMANAGE_PAGE_HISTORY`，回退时重绕目录并重放扫描位置）
- **消息框服务**: `GUI_Message.c/h` — 全局消息框（LVGL 顶层 layer 渲染）。三种形式：顶部线性动画下滑 Toast（自动消失时长由设置 `ToastDuration` 控制，1–10 秒）、屏幕中心 Confirm 对话框、Yes/No 对话框（圆角矩形 + 遮罩，模态：临时切换 keypad indev 到专用按键组）。**仅当 LVGL 持有屏幕时可用**（`lvgl_ui_is_active()`），其他线程持有屏幕时所有入口返回失败；线程安全——请求经单槽邮箱由 LVGL 定时器在 LVGL 线程执行，回调也在 LVGL 线程触发
- **生命周期**: `Page_Create(init, callback)` → `Page_Show(id)` / `Page_Show_Anim()` → `Page_Back()` → `Page_Delete(id)`
- **导航**: 内置页面历史栈，支持返回上一页
- **GUI 线程**: `GUI_Task.c`，4096 字节栈，`osPriorityAboveNormal`

### Media 播放器子系统

| 模块 | 文件 | 职责 |
|------|------|------|
| **播放器核心** | `Player.c/h` | 播放器编排、codec 抽象注册、音频数据管道、Seek 功能、播放位置回调 |
| **编解码器接口** | `Player_Codec.h` | 抽象 codec 接口：`read_pcm` / `seek` / `tell_seconds` / `close` |
| **WAV 解码** | `Player_WAV.c/h` | PCM WAV 文件解析与解码（已重构为实现 codec 接口） |
| **MP3 解码** | `Player_MP3.c/h` | MP3 解码器包装，对接 Helix 库（5120 B 窗口，统计系统） |
| **视频框架** | `Video.c/h` | AVI 播放状态机、音视频同步（PCM 时钟驱动）、独立 `VideoAudioFeedTask`、丢帧机制、帧 deadline 调度 |
| **AVI 解析** | `AVI_Parser.c/h` | RIFF/AVI 容器解析，小端读取，流索引，chunk 边界容错，Seek 支持 |
| **MJPEG 解码** | `Video_MJPEG.c/h` | MJPEG 帧解码管线，LibJPEG 流式输入，可调条带高度（`VIDEO_MJPEG_STRIPE_LINES`），JPEG arena 峰值统计 |
| **PCM 音频** | `Video_PCM.c/h` | AVI PCM 音频流解码，32 KB kfifo 缓冲，DMA 双半缓冲补数，欠载/碰撞统计，预缓冲水位线 |
| **视频内存** | `Video_Memory.c/h` | 视频帧缓冲池管理（片上 `RW_VIDEO_SRAM` 80 KB，32 B 对齐，双向链表 + 块合并），条带缓冲 |

### 内存架构

两个内存池由 `Malloc` 统一管理（`SRAMBANK = 2`），链接脚本 `StarGamer_CCM.sct` 定义 1 个 Flash 执行区 + 2 个 SRAM 执行区：

| 池 | 物理位置 | 大小 | 块 | 用途 |
|---|---------|------|------|------|
| `SRAMCCM` (0) | CCM 0x10000000 | 64 KB | 32 B | RTOS 任务栈 + CPU-only 热数据（禁止 DMA） |
| `SRAMEX` (1) | 外部 SRAM 0x68000000 | 960 KB | 32 B | DMA 缓冲、LVGL 堆、帧缓冲、GUI 上下文、PCM FIFO、I2S DMA |

- **主 SRAM（128 KB，0x20000000）**：
  - `0x20000000–0x2000BFFF`（48 KB，`RW_IRAM1`）：`.data/.bss`、MSP、C 库 heap、FreeRTOS 控制对象、NES LCD stripe 静态缓冲
  - `0x2000C000–0x2001FFFF`（80 KB，`RW_VIDEO_SRAM`）：`Video_Memory` 片上高速池（VideoTask 栈、FIL、AVI 上下文、MJPEG source、LibJPEG 上下文 + 38 KB arena、RGB 行缓冲、LCD DMA 双 stripe），80 KB 硬上限，分配失败中止视频启动
- **CCM（64 KB，0x10000000）**：64 KB 全部用作 `SRAMCCM` 堆（任务栈 + CPU-only 热数据）。**严禁放置 DMA 源/目标**
- **外部 SRAM（1 MB，0x68000000）**：960 KB 数据池 + ~60 KB 分配表 + ~4 KB guard。LVGL 堆、150 KB LCD 帧缓冲、GUI 页面上下文、32 KB PCM FIFO + 4 KB I/O 缓冲、I2S DMA 双缓冲
- **分配器**: `sys_malloc(memx, size)` / `sys_free(memx, ptr)`，含 `sys_malloc_aligned` / `sys_free_aligned` 对齐接口（宏别名 `sys_malloc_align`/`sys_free_align`）。`mymalloc`/`myfree` 宏已移除。LVGL 堆走 `SRAMEX`（`LVGL_MEM_POOL`，`lv_mem_malloc`/`lv__mem_free`/`lv__mem_realloc`）
- **线程栈**: `sys_thread_stack_alloc(size)` 从 `SRAMCCM`（CCM）分配；临时线程退出后由父上下文调用 `sys_thread_stack_free()` 回收
- **诊断**: `sysstats` 报告 CPU 和 CCM/SRAMEX 占用；`memfrag` 扫描 SRAMCCM/SRAMEX 碎片率、最大连续块和分配失败次数
- **BootLoader 预留**: Flash 低 64 KB（扇区 0–3，0x08000000–0x0800FFFF），App 起始地址 0x08010000

### 关键子系统

- **LCD**: ST7789 240×320 RGB565，SPI 接口（DMA 写入）。驱动 `Drivers/LCD/LCD.c`，扩展 RGB565 块传输 / 缩放裁剪接口。背光 PWM（TIM12 CH2），方向 `USE_HORIZONTAL = 1`。DMA 超时处理已改进
- **音频**: I2S3（SPI3）+ DMA → 外部 DAC。`BSP/i2s.c` 提供「声卡」API：
  - `audio_card_init()` / `audio_card_open_prepare()` / `audio_card_open()` / `audio_card_write()`
  - 双缓冲 DMA，回调注册（`audio_card_register_tx()`），含 DMA 碰撞统计
  - NES APU、MP3/WAV 播放器、AVI PCM 均通过回调推送到 I2S
- **存储**:
  - SD 卡 SDIO 4-bit + DMA（RX: DMA2_Stream3，TX: DMA2_Stream6），FatFs codepage 936，已启用 512 B 对齐 scratch buffer
  - W25Q64（8 MB）SPI Flash（SPI2）分区：字体区 2 MB（0x000000）+ 内存转储区 3.5 MB（0x200000）+ 设置区 512 KB（0x580000，双 4 KB 槽）+ 固件暂存区 1 MB（0x600000）+ 固件备份区 1 MB（0x700000）
  - AT24C02（256 B）I2C EEPROM：用于 IAP 双槽容错记录 + 系统设置持久化
- **NES 模拟器**: 入口 `nes_main.c`，核心 `nes_ppu.c`/`nes_apu.c`/`nes_mapper.c`/`nes_crc32.c`/`6502.s`，`NES_SKIP_FRAME = 1`（跳帧），mapper 目录含 118 个 `.cpp` + 126 个 `.h`（`000.cpp` 起）。LCD 双 stripe（16 行 × 2）为静态数组（主 SRAM `RW_IRAM1`）；APU 经 `audio_card_register_tx_index(nes_i2s_dma_tx_callback)` 推送 I2S，含 `NES_AudioStats_t` 统计
- **Arduboy 游戏**: `Middlewares/Third_Party/Arduboy/` 含兼容层（Arduboy2 引擎核心）+ 3 款游戏（Buttons / Dino / FlappyBall）。128×64 1bpp framebuffer，通过 `APP/Arduboy/Arduboy_Runtime.c` 管理生命周期，`Arduboy_Port.c` 对接 LCD 显示和按键
- **Helix MP3**: `Middlewares/libhelix/` 定点 Layer III 解码器，14 个 C 文件 + 2 个 ARM 汇编优化（`asmmisc_rvds.s`、`asmpoly_thumb2_rvds.s`）+ 移植适配层 `port/helix_support.c`，通过 `Player_MP3.c` 适配 codec 接口
- **USB MSC**: `USB_DEVICE/`，SD 卡模拟 U 盘，通过 CubeMX USB OTG FS 驱动 + ST 官方 USB 设备库实现
- **字库**: XBF 格式字库从 SD 卡加载到 SPI Flash（起始地址 0，2 MB 预留）。`myFont.c` 提供代理 `lv_font_t`。支持字库卸载。字库参数持久化在 `System_Settings_t`
- **系统设置**: `System_Status.c` 的 `System_Settings_t` 持久化到 AT24C02 + W25Q64 设置区（双槽镜像）：音量/背光/USB MSC/主题/字库参数/AXF 栈档位/时钟档位/内存转储区域开关/调试日志开关。时钟三档 `System_ClockProfile_t`（192/216/240 MHz，默认 240 MHz）；AXF 栈五档（4/8/12/16/24 KB）；调试日志四类 `System_DebugLog_t`（AUDIO/VIDEO/LIBJPEG/FATFS）
- **HardFault 处理**: `firmware_start.c` 内嵌完整 HardFault Handler — 独立重新初始化 SPI1 + LCD GPIO，在 LCD 上绘制寄存器转储（R0-R3/R12/LR/PC/xPSR/CFSR/HFSR/DFSR/AFSR/MMFAR/BFAR + MSP/PSP），无需依赖 HAL 或 RTOS。内存分配 HardFault 已修复
- **按键**: 8 键驱动（`BSP/key.c`），`KEY_Id_t` 枚举 + 位掩码宏，预定义游戏键位别名（`KEY_GAME_*`）与 NES 别名（`KEY_NES_*`）。引脚：UP=PB1 / DOWN=PA1 / LEFT=PC1 / RIGHT=PA2 / OK·START=PE2 / A=PE3 / B=PG14 / SELECT·EXIT=PB7
- **IAP 固件更新**: `IAP/IAP.c` — `IAP_PrepareUpdate()` 从 SD 卡 `0:/update.bin` → CRC32 校验 → W25Q64 暂存 → EEPROM 双槽记录。支持断电恢复、中断向量验证、进度回调。共享协议 `IAP_Protocol.h` 定义 flash 分区、W25Q64 暂存/备份/设置区、EEPROM 双槽（SLOT0:128/SLOT1:168）与记录结构（magic `BLI2` v2，36 字节）
- **Memory_Dump**: `System/Memory_Dump.c` — HardFault 时自动将 CCM/主 SRAM/外部 SRAM 内存映像写入 W25Q64 转储分区（3.5 MB，0x200000），可经 Shell 命令导出到 SD 卡 `hardfault_dump.bin`。支持 FNV-1a 校验和、进度回调、分区边界编译期检查、按区域使能（`MemoryDumpCcmEnabled` 等设置）

### BootLoader

独立 CubeMX 工程，位于 `BootLoader/StarGamerBootLoader/`，设计文档见 `Docs/BootLoader-Design.md`：

- **Flash 分区**: BootLoader 64 KB（扇区 0–3，0x08000000–0x0800FFFF），Application 960 KB（扇区 4–11，0x08010000–0x080FFFFF）
- **更新流程**: SD 卡 `update.bin` → 写入 W25Q64 暂存区（0x600000）→ CRC32 校验 → 擦写内部 Flash → 校验后跳转 App
- **安全机制**: 双备份（W25Q64 备份区 0x700000）、EEPROM 双槽记录、断电容错、CRC 全程校验
- **外设**: 仅依赖 SPI1、SPI2（LCD / W25Q64 / 按键）、TIM13、内部 Flash HAL、CRC
- **模块化分层**（`Boot/`）: `bootloader.c`（顶层编排）、`boot_crc.c`（CRC32）、`boot_storage.c`（存储抽象）、`boot_ui.c`（LCD 显示）、`boot_platform.h`（平台类型定义）
- **产物**: 已编译生成 `StarGamerBootLoader.hex`、`.bin`、`.axf`

### LVGL 集成

- **LVGL 线程**: 独立线程 `lvgl_timer_task`（任务名 `LVGL`），8192 字节栈（`LVGL_THREAD_STACK_SIZE`），`osPriorityNormal`，每 1ms（`osDelay(1)`，tick 1 kHz）调用 `lv_timer_handler()`，经 `lvgl_ui_pause_point()` 感知视频独占
- **显示驱动**: `lv_port_disp_template.h`，使用外部 SRAM 帧缓冲
- **输入设备**: `lv_port_indev_init()` 注册按键输入
- **视频播放期间**: 通过 `lvgl_stop_ui()` / `lvgl_start_ui()` 暂停/恢复 LVGL，释放 LCD/SPI1 所有权给 Video Task
- **启动安全**: 修复了 LVGL 与 GUI 启动时线程静态变量引发的 HardFault
- **主题**: `GUI_Theme.c/h` 提供深色/浅色双主题，通过 `GUI_Theme_Apply()` 一键切换，所有 GUI 页面通过 `GUI_Theme_Color()` / `GUI_Theme_LvColor()` 获取统一配色

### ELF 动态加载器

`ELF Loader/` 目录提供运行时动态加载能力，适用于 Cortex-M 系列：

- **核心**: `dl_lib.c/h` — 加载/卸载/符号查找（`dl_load_lib`、`dl_get_func`、`dl_destroy_lib`）
- **ELF 解析**: `dl_elf.h` — 完整的 ELF 格式解析结构体
- **架构适配**: `dl_arch.c` — 重定位指令处理
- **移植层**: `dl_port.c/h` — 内存分配/文件 IO 抽象
- **向量表**: `dl_vector.c` — 函数导出表，通过 SVC 8 调用让动态加载代码访问宿主 C 标准库（memcpy/memset/strlen 等 22 个函数）；`dl_vector_api.c` — 扩展向量表 API 导出，对接 `EXTERN API/` 模块
- **标准库 shim**: `common/` — `dl_extern_lib.h`（SVC 调用宏）/ `dl_string_lib.h` / `dl_stdio_lib.h` / `dl_stdlib_lib.h` / `dl_time_lib.h`
- **用户 API**: `dl_user_api.h` — 用户侧 API 声明（含 LCD 控制、按键、延时等 13 个 AXF 用户函数），与 `Star Gamer ELF/dl_User_api.h` 保持同步
- **演示工程**: `Star Gamer ELF/` — 独立 Keil 工程，构建 AXF demo 应用；`Tests/AXF_Loader_Host/validate_axf.py` — 主机端 AXF 兼容性验证脚本

### AVI 视频播放器架构

当前 AVI 播放器支持 MJPEG + PCM 音视频同步播放，主要优化方向见 `Docs/AVI高码率卡顿优化方案.md`：

- **已知问题**: 高码率下 DMA 碰撞率约 18%–21%，音频补缓冲事件可能丢失，JPEG 解码耗时峰值 66–75ms
- **优化方向**: P0 修复音频 DMA 事件丢失和前瞻队列阻塞，P1 继续降低 JPEG CPU 时间
- **画面撕裂**: 无 TE 同步信号，根因分析见 `Docs/AVI视频撕裂问题结论.md`

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
- **内存池选择**: DMA 缓冲区 **必须**走 `SRAMEX` 或片上 `Video_Memory` 池；RTOS 任务栈与 CPU-only 热数据走 `SRAMCCM`（禁止 DMA）
- **IAP 共享协议**: App 和 BootLoader 通过 `IAP/IAP_Protocol.h` 共用 flash 分区地址、EEPROM 槽位定义、记录结构体宏 `IAP_RECORD_MEMBERS`

## 常见陷阱

1. **CCM DMA 限制**: 绝不能从 `SRAMCCM` 分配 DMA 缓冲区——DMA 控制器无法访问 CCM。DMA 缓冲区使用 `SRAMEX` 或片上 `Video_Memory` 池（`RW_VIDEO_SRAM`）
2. **CubeMX 重新生成**: 始终将自定义代码放在 `USER CODE` 块内。主固件和 BootLoader 各有独立的 CubeMX 工程
3. **类型不匹配**: 使用 `u8`/`u16`/`u32`，而非标准 `uint8_t` 类型
4. **RTOS API**: 使用 CMSIS-RTOS V2（`cmsis_os2.h`），而非原生 FreeRTOS 调用
5. **外部 SRAM 地址**: 外部 SRAM 起始地址为 0x68000000，非 0x20000000（内部 SRAM）
6. **GBK 编码**: 编辑含中文注释/字符串的源文件需注意 GBK 编码，避免乱码
7. **HardFault 调试**: HardFault 信息会自动显示在 LCD 上（320×240，5×7 字体），无需串口或调试器即可查看寄存器状态
8. **LCD 接口**: 本工程 ST7789 走 **SPI** 接口（非 FSMC 并行）。视频播放期间 Video Task 独占 SPI1/LCD
9. **SDIO DMA 对齐**: FatFs 通过 `ENABLE_SCRATCH_BUFFER` 提供 512 B 对齐缓冲区，避免 DMA word 对齐覆盖压缩流边界
10. **GUI 页面上下文**: 页面级 `static` 全局变量（内嵌 `GUI_FileManage_t`）占用主 SRAM（`RW_IRAM1` 共 48 KB），新增页面应优先从 SRAMEX 动态分配
11. **视频 DMA 碰撞**: 高码率 AVI 播放时音频 DMA 事件可能合并丢失，`collide/dma` 统计指标需关注；相关优化方案见 `Docs/AVI高码率卡顿优化方案.md`
12. **IAP 记录**: EEPROM 双槽（SLOT0: 128, SLOT1: 168）通过序列号实现断电安全写入；修改 `IAP_RECORD_MEMBERS` 前确认 `IAP_RECORD_SIZE` 一致性

## 备注

- 根目录 `myFont.c` + `myFont.bin`（1 MB）为运行时字库代理和默认字库二进制
- `reasonix.toml` 为 Reasonix 权限配置文件
- `.agents/skills/arm-cortex-expert/` 为 ARM Cortex-M 专家技能
- `BootLoader/StarGamerBootLoader/` 为独立 CubeMX 工程，与主固件共享 Drivers/BSP 部分代码，已编译可用
- `Star Gamer ELF/` 为独立 Keil 工程，用于构建 AXF demo 应用；`Star Gamer ELF - 2048/` 为 2048 游戏 AXF 应用工程，产物分别放 SD 卡根目录由 AXF Loader 页面加载
- `IAP/` 已完整实现（含跳转前 Flash ART 缓存刷新、CONTROL 寄存器清零等加固措施），App 和 BootLoader 通过 `IAP_Protocol.h` 共享协议定义
- `PCB/` 含立创 EDA 硬件设计工程文件
- `Docs/` 目录含详细设计文档，修改前应查阅相关文档；`Docs/内存规划重构.md` 描述当前 SRAM 分区布局
- `Tests/` 含 AVI 解析器 PC 端回归测试 + AXF 加载器主机端验证 + SDIO IRQ 优先级验证脚本（AVI 样本需自备，不入库）
- `Middlewares/libhelix/port/helix_support.c` 为 Helix MP3 移植适配层
- `Middlewares/Third_Party/BMP/` 为 BMP 图像库占位，尚未实现
- ELF Loader 源自 `dl_lib`（Cortex-M 系列通用），当前适配 STM32F407；AXF 应用通过 `stargamer_axf_api.h`（`SG_*` 前缀）使用 SVC 调用宿主 LCD/按键/延时 API
- Memory_Dump 使用 W25Q64 的 3.5 MB 内存转储分区（0x200000），通过编译期断言验证分区边界不重叠
- `临时问题固件/` 为临时调试用固件副本（.bin/.axf/.map），不纳入版本管理
