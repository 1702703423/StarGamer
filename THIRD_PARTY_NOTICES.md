# 第三方组件声明 / Third-Party Notices

本仓库（Star Gamer）以 **GNU General Public License v3.0** 开源，全文见根目录 [`LICENSE`](LICENSE)。

> **重要：GPL-3.0 仅覆盖本项目的原创部分。**
>
> 仓库中包含的第三方组件由各自的版权持有人按各自的许可条款授权，**不受 GPL-3.0 约束，
> 也不能被本项目重新授权为 GPL-3.0**。就这些组件而言，其自身许可条款优先于 GPL-3.0 适用。
>
> 换言之：本项目对原创代码授予 GPL-3.0 权利；对第三方组件，本项目**不授予任何额外权利**，
> 使用者须自行遵守各组件的原始许可。

> ⚠️ **已知版本冲突（2026-10-14 核实）**
>
> `ELF Loader/` 中的动态加载器取自上游 **`dynamic_loader`**（© wzh，
> Gitee: `wzh1845462801/dynamic_loader`），该上游的许可为 **GPL-2.0**。
> **GPL-2.0 与 GPL-3.0 互不兼容**，因此本仓库目前**无法合法地整体以 GPL-3.0 分发**。
> 详见第三节第 4 项。

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
| `ELF Loader/`（仅 `dl_vector_api.c`、`dl_user_api.h`，及对 `dl_port.c`/`dl_vector.c` 的项目适配改动）| 用户 API 编号、SVC 宿主函数分发（**该目录其余文件均为第三方 GPL-2.0 代码或其衍生，见第二节**）|
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
| **dynamic_loader（ELF 动态加载器）** | `ELF Loader/` 下除 `dl_vector_api.c`、`dl_user_api.h` 外的全部文件（含 `dl_lib.*`、`dl_elf.h`、`dl_arch.c`、`dl_port.*`、`dl_vector.c`、`common/`）| 1.0（2021-12-3，© wzh）| **GPL-2.0** | ❌ **不兼容** |
| **Helix MP3 解码器** | `Middlewares/libhelix/` | — | **RCSL 1.0 / RPSL 1.0** | ❌ **不兼容** |
| **NES 模拟器** | `Middlewares/Third_Party/NES/` | — | **未声明** | ⚠️ **不明** |
| **Arduboy 兼容层** | `Middlewares/Third_Party/Arduboy/` | — | **未声明** | ⚠️ **不明** |

---

## 三、需要特别注意的组件

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

**上游核实（2026-10-14）**：常见的嵌入式分发仓库
[`ultraembedded/libhelix-mp3`](https://github.com/ultraembedded/libhelix-mp3) 的 `LICENSE.txt` 原文为：

```
Copyright (c) 1995-2004 RealNetworks, Inc. All Rights Reserved.

The contents of this directory ... are subject to the current version of the
RealNetworks Public Source License (the "RPSL") available at RPSL.txt in this
directory, unless you have licensed the directory under the current version of
the RealNetworks Community Source License (the "RCSL") ...
```

该仓库根目录仅有 `LICENSE.txt`、`RPSL.txt`、`RCSL.txt` 三个许可文件，
**不存在任何宽松许可（无 BSD / MIT / Apache）**。其他分发渠道（如 Microchip 的
`helix_mp3`）同样标注 RPSL / RCSL。

> 结论：**Helix MP3 解码器不存在宽松许可版本**，任何分发都必须走 RPSL 或商业 RCSL。
> 因此若要坚持 GPL-3.0，只能更换解码器实现，不能靠"换个 fork"绕过。

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

### 4. dynamic_loader（ELF 动态加载器）—— GPL-2.0，与 GPL-3.0 不兼容

**这是本仓库目前最需要处理的一项：问题不是"缺少声明"，而是"有声明但版本冲突"。**

`ELF Loader/` 下的动态加载器取自上游项目 **`dynamic_loader`**：

| 项 | 值 |
|---|---|
| 原始仓库 | `https://gitee.com/wzh1845462801/dynamic_loader` |
| GitHub 镜像 | `https://github.com/VarleyT/dynamic_loader`（README 自述"拷贝自"上述 Gitee 仓库）|
| 作者 / 版本 | wzh，v1.0（2021-12-3）|
| **许可** | **GPL-2.0**（Gitee 仓库信息标注 `License: GPL-2.0`；GitHub 徽标 `GPL-2.0`；`LICENSE` 为标准 GPLv2 全文，未作 "or later" 修改）|

#### 为什么这是硬冲突

**GPL-2.0 与 GPL-3.0 互不兼容**，这是自由软件基金会明确记载的经典不兼容情形：

- GPLv2 §6 规定"不得对接受者行使本许可授予的权利附加任何进一步限制"；
- GPLv3 新增了反 Tivoization（§6）、专利报复（§10–11）等条款，
  恰是 GPLv2 §6 所禁止的"进一步限制"。

因此 **GPL-2.0 代码与 GPL-3.0 代码不能合并进同一作品**。本项目的固件是单一链接产物，
`ELF Loader/` 的代码被编译进固件、构成同一作品 —— **故本仓库不能整体声明 GPL-3.0**。

> 若上游实际意图为 "GPL-2.0-or-later"，此冲突即不成立。但当前 LICENSE 为未修改的 GPLv2
> 全文，Gitee / GitHub 均标注 GPL-2.0，源文件头亦无 "or later" 授权语句，
> 故应按 **GPL-2.0-only** 保守理解。

#### 涉及文件

`ELF Loader/` 目录下除 `dl_vector_api.c`、`dl_user_api.h` 外的全部文件：

- `dl_lib.c`、`dl_lib.h`、`dl_arch.c`、`dl_elf.h`、`dl_port.c`、`dl_port.h`
  —— 文件头均署名 `wzh`，与上游 `src/` 同名
- `dl_vector.c` —— 上游同有此文件（署名 `wzh`）；本项目版本已改写（去掉文件头、改为引用
  本项目内部头文件），但属**衍生作品**，仍受 GPL-2.0 约束
- `common/` 下的 `dl_extern_lib.h`、`dl_stdio_lib.h`、`dl_stdlib_lib.h`、`dl_time_lib.h`
  —— 与上游 `common/` 同名同构；`dl_string_lib.h` 为本项目新增（沿用上游写法）

> 仅 `dl_vector_api.c`、`dl_user_api.h`，以及对 `dl_port.c` / `dl_vector.c` 的项目适配改动
> 属于本项目原创。

> 另注：该上游代码自述"移植自 RT-Thread 的动态加载模块"（`dl_elf.h:7`）。RT-Thread 采用
> Apache-2.0，与 GPL-3.0 兼容，故 RT-Thread 一侧不构成障碍；问题在于上游将整体置于 GPL-2.0。

#### 可选处理方式

1. **向上游作者请求放宽许可**（推荐）—— 请求 wzh 将 `dynamic_loader` 改为
   **GPL-2.0-or-later** 或改用宽松许可。上游仓库 2026-10-13 仍有更新、作者活跃，
   沟通可行性较高；拿到 "or later" 后 GPL-3.0 即可覆盖它。
2. **重写该模块** —— 加载器共 6 个源文件（约 40 KB）。`dl_arch.c` 已注明参考 ARM ELF
   规范（`IHI0044`）与 RT-Thread 实现，可据此自行实现以彻底摆脱 GPL-2.0。
3. **整体改回 GPL-2.0** —— 但会引出新冲突：`Drivers/CMSIS/` 为 **Apache-2.0**，而
   Apache-2.0 与 **GPL-2.0** 不兼容（专利终止与赔偿条款）。等于用一个冲突换另一个。
4. **分离分发** —— 拆为独立仓库、不随固件分发。对静态链接固件主张"独立模块"说服力弱，
   不建议作为主要方案。

---

## 四、再分发义务提示

若你分发本项目（源码或固件），须同时注意：

1. **保留本声明与各组件的许可文件**，不得移除或篡改第三方版权声明。
2. **GPL-3.0 §6（Installation Information）**：分发设备形态的产品时，须提供安装修改后
   固件的手段。本项目具备 IAP + BootLoader（SD 卡刷机）能力，但若对固件施加签名校验或
   密钥锁定，将违反该条款。
3. **完整对应源码**：GPL-3.0 要求提供完整的对应源码，包含用于构建固件的脚本与配置。
4. **上述第 1～4 项问题组件**（Helix / NES / Arduboy / dynamic_loader）在授权厘清前，
   不应随固件分发。
5. **GPL 版本冲突解决前，请勿对外宣称本项目为 GPL-3.0。** `ELF Loader/` 含 GPL-2.0
   代码，与 GPL-3.0 无法合并（详见第三节第 4 项）。

---

## 五、说明

- 本文件为**事实性清单**，便于使用者了解各组件的授权状态，不构成法律意见。
  涉及实际分发时，建议咨询专业人士。
- 组件版本号取自仓库内文件（如 `lvgl.h`、`shell.h` 的版本宏）。
- 若后续新增或移除第三方组件，请同步更新本文件。

_最后更新：2026-10-14_
