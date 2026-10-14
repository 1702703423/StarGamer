# 第三方组件声明 / Third-Party Notices

本仓库（Star Gamer）以 **GNU General Public License v3.0** 开源，全文见根目录 [`LICENSE`](LICENSE)。

> **重要：GPL-3.0 仅覆盖本项目的原创部分。**
>
> 仓库中包含的第三方组件由各自的版权持有人按各自的许可条款授权，**不受 GPL-3.0 约束，
> 也不能被本项目重新授权为 GPL-3.0**。就这些组件而言，其自身许可条款优先于 GPL-3.0 适用。
>
> 换言之：本项目对原创代码授予 GPL-3.0 权利；对第三方组件，本项目**不授予任何额外权利**，
> 使用者须自行遵守各组件的原始许可。

---

## 一、GPL-3.0 覆盖范围（本项目原创部分）

以下目录为本项目原创，以 GPL-3.0 授权：

| 路径 | 内容 |
|---|---|
| `Core/` | 入口、时钟与外设初始化、FreeRTOS 配置、中断服务 |
| `System/` | CCM/SRAMEX 双堆内存分配器、系统状态与设置持久化、HardFault 转储、`kfifo`、链表 |
| `BSP/` | W25Q64、AT24C02、I2S 音频、按键驱动 |
| `GUI/` | LVGL 页面管理器、主题系统、消息框服务、文件浏览器 |
| `Media/` | 播放器核心与 codec 抽象、WAV/MP3 解码封装、AVI 解析、MJPEG 解码、PCM 音频 |
| `APP/Arduboy/` | Arduboy 运行时与硬件移植层（**不含** `Middlewares/Third_Party/Arduboy/`）|
| `IAP/` | 在应用编程模块 |
| `ELF Loader/` | 动态 ELF 加载器 |
| `EXTERN API/` | AXF 动态加载宿主端 API |
| `Debug/` | 调试命令、性能统计（**不含** Letter Shell，见下）|
| `Tools/` | PC 端工具（hexdump、map_analyzer）|
| `Docs/` | 架构与设计文档 |
| `Tests/` | PC 端回归测试 |
| `Star Gamer ELF/`、`Star Gamer ELF - 2048/` | AXF 示例应用与 2048 游戏 |
| `BootLoader/`（`Drivers/` 子目录除外）| 独立 BootLoader 工程 |
| `AGENTS.md`、`CLAUDE.md`、`LICENSE` | 项目文档与许可 |

> `FATFS/`、`LIBJPEG/`、`USB_DEVICE/` 中的 `App/`、`Target/` 目录为 STM32CubeMX
> 生成的适配层代码，按 ST 的生成代码条款使用。

---

## 二、第三方组件清单

| 组件 | 路径 | 版本 | 许可 | 与 GPL-3.0 兼容 |
|---|---|---|---|---|
| FreeRTOS Kernel | `Middlewares/Third_Party/FreeRTOS/` | v10.3.1 | MIT (Amazon.com, Inc.) | ✅ 是 |
| LVGL | `Middlewares/Third_Party/LVGL/` | 8.2.0 | MIT | ✅ 是 |
| FatFs | `Middlewares/Third_Party/FatFs/src/` | R0.12c | ChaN BSD 风格（单条件 BSD）| ✅ 是 |
| FatFs ST 适配层 | `FATFS/` | — | BSD-3-Clause (STMicroelectronics) | ✅ 是 |
| LibJPEG | `Middlewares/Third_Party/LibJPEG/` | IJG 系 | IJG License (Independent JPEG Group) | ✅ 是 |
| Letter Shell | `Debug/shell.c`、`shell.h`、`shell_cfg.h` 等 | 3.1.2 | MIT (© Letter / NevermindZZT) | ✅ 是 |
| CMSIS | `Drivers/CMSIS/`、`BootLoader/StarGamerBootLoader/Drivers/CMSIS/` | — | Apache-2.0 | ✅ 是 |
| STM32F4xx HAL Driver | `Drivers/STM32F4xx_HAL_Driver/` | — | ST Package License（BSD-3 系）| ✅ 是 |
| STM32 USB Device Library | `Middlewares/ST/STM32_USB_Device_Library/` | — | ST Package License（BSD-3 系）| ✅ 是 |
| BMP（占位）| `Middlewares/Third_Party/BMP/` | — | 文件为 0 字节，无实质内容 | ✅ 是 |
| **Helix MP3 解码器** | `Middlewares/libhelix/` | — | **RCSL 1.0 / RPSL 1.0** | ❌ **不兼容** |
| **NES 模拟器** | `Middlewares/Third_Party/NES/` | — | **未声明** | ⚠️ **不明** |
| **Arduboy 兼容层** | `Middlewares/Third_Party/Arduboy/` | — | **未声明** | ⚠️ **不明** |

---

## 三、需要特别注意的三项

### 1. Helix MP3 解码器 —— 与 GPL-3.0 不兼容

`Middlewares/libhelix/` 各源文件头部声明：

```
/* ***** BEGIN LICENSE BLOCK *****
 * Version: RCSL 1.0/RPSL 1.0
 *
 * Portions Copyright (c) 1995-2002 RealNetworks, Inc. All Rights Reserved.
 * ...
 * subject to the current version of the RealNetworks Public Source License
 * Version 1.0 (the "RPSL") available at
 * http://www.helixcommunity.org/content/rpsl ...
```

RealNetworks Public Source License 1.0 被自由软件基金会（FSF）归类为**自由软件但不兼容 GPL**。
因此 **不能将 libhelix 与本项目一起以 GPL-3.0 分发**。

如需使整个固件可依 GPL-3.0 分发，须将 MP3 解码器替换为 GPL 兼容实现，例如：

- `minimp3` —— CC0（公共领域贡献）
- `dr_mp3` —— MIT-0 / Public Domain

### 2. NES 模拟器 —— 来源与授权不明

`Middlewares/Third_Party/NES/` 源文件仅含以下来源注释，**未附带任何许可声明**：

```
//本程序移植自网友ye781205的NES模拟器工程
//ALIENTEK STM32F407开发板
//正点原子@ALIENTEK
//技术论坛:www.openedv.com
//创建日期:2014/7/1
```

原始作者的授权范围无从确认。**在取得明确授权前，含有该组件的固件不应再分发**——
这一点与选择何种开源协议无关。

### 3. Arduboy 兼容层 —— 来源与授权不明

`Middlewares/Third_Party/Arduboy/README.md` 自述：

> The imported files did not include upstream URLs, version tags, copyright notices,
> or license texts. Their provenance must be established before a firmware image
> containing them is redistributed.

即：上游来源、版权与许可均缺失，**再分发前必须先厘清来源**。

---

## 四、再分发义务提示

若你分发本项目（源码或固件），须同时注意：

1. **保留本声明与各组件的许可文件**，不得移除或篡改第三方版权声明。
2. **GPL-3.0 §6（Installation Information）**：分发设备形态的产品时，须提供安装修改后
   固件的手段。本项目具备 IAP + BootLoader（SD 卡刷机）能力，但若对固件施加签名校验或
   密钥锁定，将违反该条款。
3. **完整对应源码**：GPL-3.0 要求提供完整的对应源码，包含用于构建固件的脚本与配置。
4. **上述第 1、2、3 项问题组件**（Helix / NES / Arduboy）在授权厘清前，不应随固件分发。

---

## 五、说明

- 本文件为**事实性清单**，便于使用者了解各组件的授权状态，不构成法律意见。
  涉及实际分发时，建议咨询专业人士。
- 组件版本号取自仓库内文件（如 `lvgl.h`、`shell.h` 的版本宏）。
- 若后续新增或移除第三方组件，请同步更新本文件。

_最后更新：2026-10-14_
