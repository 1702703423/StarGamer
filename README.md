# Star Gamer

基于 **STM32F407ZGT6** 的掌上游戏机固件。单芯片集成 NES 模拟器、Arduboy 游戏运行环境、AVI 视频播放、MP3 / WAV 音乐播放、图片与文本浏览，AXF动态加载，以及支持 SD 卡刷机的 IAP + BootLoader。

## 硬件与工具链

| 项目 | 参数 |
|---|---|
| MCU | STM32F407ZGT6，HSE 8 MHz → PLL 超频，192 / 216 / 240 MHz 三档可切换（默认 240 MHz） |
| 内存 | 1 MB Flash，192 KB 内部 SRAM，1 MB 外部 SRAM（0x68000000），8 MB SPI NOR（W25Q64） |
| 显示 | ST7789，320×240，RGB565，SPI + DMA |
| 音频 | I2S3 + DMA → 外部 DAC |
| 存储 | SD 卡（SDIO 4-bit + DMA，FatFs）、AT24C02 EEPROM |
| 软件栈 | FreeRTOS v10.3.1（CMSIS-RTOS V2，静态分配）、LVGL 8.2.0、FatFs R0.12c、LibJPEG、Helix MP3 |
| 工具链 | Keil MDK-ARM V5.32（ARMCLANG / AC6） |

## 已测试的多媒体规格

以下为本机实测可正常播放的规格，超出后可能出现掉帧或音频欠载（详见 `Docs/AVI高码率卡顿优化方案.md`）。

| 类型 | 格式 | 实测参数 |
|---|---|---|
| 音频 | MP3 | **44100 Hz / 320 kbps** |
| 音频 | WAV（PCM） | **44100 Hz / 320 kbps** |
| 视频 | AVI（MJPEG + PCM） | **24 FPS**，视频 **2.64 Mbps**，音频 PCM **22050 Hz / 705 kbps** |

> 播放器按"音频时钟驱动视频"同步，音频优先不丢帧；视频超时会主动丢帧以维持音画同步。
> 测试用的音视频样本体积较大，未纳入版本库，需自备。

## 构建

1. 主固件：Keil 打开 `MDK-ARM/Star Gamer.uvprojx`，**F7** 编译，**F8** 下载。
2. BootLoader：打开 `BootLoader/StarGamerBootLoader/MDK-ARM/StarGamerBootLoader.uvprojx`，同上。
3. 工程无 Makefile / CMake，构建完全在 Keil IDE 内完成，编译后自动生成 `.bin`。

## ⚠️ 请勿使用工程内的 `.ioc` 重新生成代码

仓库中的 `Star Gamer.ioc` 与 `BootLoader/StarGamerBootLoader/StarGamerBootLoader.ioc` **仅作为引脚与外设配置的记录保留**，不能用于生成代码。

原因：

- 当前固件已在生成代码基础上做了大量手工修改——240 MHz 超频、自定义链接脚本 `System/StarGamer_CCM.sct`（1 个 Flash 执行区 + 2 个 SRAM 执行区）、CCM / 外部 SRAM 双堆、DMA 缓冲区落位、FreeRTOS 静态内存分配等，**这些都不是 CubeMX 能表达的内容**。
- 重新生成会覆盖 `Core/`、`Drivers/`、`FATFS/`、`LIBJPEG/`、`USB_DEVICE/` 等目录，`USER CODE` 块以外的改动全部丢失，链接脚本与内存布局随之失效，工程将无法编译或启动即 HardFault。
- `.ioc` 中的时钟、DMA、中断优先级等参数已与最终固件不一致，即使只做"增量生成"也会引入偏差。

**需要调整硬件配置时，请直接手改 `Core/Src/` 下的初始化代码与链接脚本，不要走 CubeMX。**

## HardFault 与 DUMP 导出

固件内置独立的 HardFault 处理（不依赖 HAL / RTOS，重新初始化 SPI1 与 LCD GPIO 后直接绘制）：

1. **屏幕直读**：在 LCD 上打印 `R0-R3 / R12 / LR / PC / xPSR / CFSR / HFSR / DFSR / AFSR / MMFAR / BFAR` 以及 `MSP / PSP`，无需调试器即可定位现场。
2. **内存转储**：同时把 CCM、内部 SRAM、外部 SRAM 的内存映像写入 W25Q64 的转储分区（3.5 MB @ `0x200000`），带 FNV-1a 校验和；各区域可按设置单独开关。
3. **导出到 SD 卡**：下次上电时自动把待导出转储写到 **`0:/hardfault_dump.bin`**。
4. **解析**：用**与该固件完全对应**的 `.axf` + `.map` 还原符号与调用栈。`HardFault -dump/` 下保留了一份示例（含对应 axf/map），格式参考 `hardfault_dump-1.1.4-<PC>-<MSP|PSP>-<HFSR|...>.bin`。

> 注意：转储依赖 W25Q64 与 SD 卡同时可用；若故障发生在 SD 卡挂载前，转储会留在 Flash 中等待下一次上电导出。

## 开源协议

本项目原创部分以 **GPL-3.0** 发布，但**存在尚未解决的许可冲突，当前不能整体按 GPL-3.0 分发**：

| 组件 | 位置 | 许可 | 与 GPL-3.0 |
|---|---|---|---|
| ELF 动态加载器（上游 `dynamic_loader`，© wzh） | `ELF Loader/` | GPL-2.0 | ❌ GPL-2.0 与 GPL-3.0 互不兼容 |
| Helix MP3 解码器 | `Middlewares/libhelix/` | RPSL 1.0 / RCSL 1.0 | ❌ RPSL 明文禁止套用 GPL |
| NES 模拟器 | `Middlewares/Third_Party/NES/` | 未声明 | ⚠️ 授权不明 |
| Arduboy 兼容层 | `Middlewares/Third_Party/Arduboy/` | 未声明 | ⚠️ 授权不明 |
| FreeRTOS / LVGL / FatFs / LibJPEG / Letter Shell / CMSIS / ST HAL | 见声明文件 | MIT / BSD / Apache-2.0 / ST | ✅ 兼容 |

- 固件是**单一静态链接镜像**，上述组件与原创代码编译进同一个 `.bin`，"改成库链接 / 单独放目录"不能规避冲突。
- 在授权厘清前，**请勿再分发包含上述组件的固件二进制**，也请勿对外宣称本仓库整体为 GPL-3.0。
- 完整清单、逐条法律依据与可选处理方案见 **[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)**；该文件为事实性说明，不构成法律意见。

## 目录速览

```
Core/ System/ BSP/ Drivers/       入口、内存分配、系统状态、驱动
GUI/ Media/ APP/                  页面管理器、播放器与 codec、Arduboy 运行时
Middlewares/                      FreeRTOS / LVGL / FatFs / LibJPEG / Helix / NES
IAP/ BootLoader/                  固件更新与独立 BootLoader 工程
ELF Loader/ EXTERN API/           AXF 动态加载与应用侧 API
Debug/ Tools/ Tests/ Docs/        调试命令、PC 工具、回归测试、设计文档
```

更多细节见 [`AGENTS.md`](AGENTS.md) 与 `Docs/`。
