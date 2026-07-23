# Star Gamer — BootLoader 固件更新方案设计

> **目标平台**: STM32F407ZGT6（Cortex-M4F @ 168 MHz，1 MB 内部 Flash，192 KB 内部 SRAM + 1 MB 外部 SRAM）
> **设计原则**: 最小侵入、断电容错、双备份兜底、CRC 全程校验
> **版本**: v1.1
> **状态**: 已实现并通过 Keil ARMCLANG 编译；板级掉电/恢复测试待完成

> **实现基线（2026-07-23）**：共享协议位于 `IAP/IAP_Protocol.h`；APP 端由
> `IAP/IAP.c` 和 `GUI/GUI_Settings.c` 完成 `update.bin` 暂存并自动复位；裸机端位于
> `BootLoader/StarGamerBootLoader/Boot/`。本文以下流程以当前实现为准。

实际硬件配置：HSE 为 8 MHz；SPI2 SCK 使用 PB13；LCD 使用 SPI1（PC4 CS、PC5 DC、
PB12 RST、PB15 BL），W25Q64 使用 SPI2（PE6 CS），AT24C02 使用 PB8/PB9 软件 I2C。
APP 按项目要求保持 APB2 168 MHz、SPI1 84 MHz；BootLoader 使用 APB2 84 MHz、SPI1
42 MHz。APP 的 APB2 配置高于 STM32F407 数据手册额定 84 MHz，属于明确保留的超频配置。

---

## 目录

1. [系统架构与内存布局](#1-系统架构与内存布局)
2. [更新流程](#2-更新流程)
3. [BootLoader 功能](#3-bootloader-功能)
4. [CRC32 校验机制](#4-crc32-校验机制)
5. [安全与容错设计](#5-安全与容错设计)
6. [实施清单](#6-实施清单)

---

## 1. 系统架构与内存布局

### 1.1 内部 Flash 分区（STM32F407ZG，共 1 MB @ 0x08000000）

STM32F407ZG 内部 Flash 共 12 个扇区，物理特性如下：

| 扇区 | 地址范围 | 大小 | 用途 |
|------|----------|------|------|
| Sector 0 | `0x08000000` – `0x08003FFF` | 16 KB | **BootLoader** |
| Sector 1 | `0x08004000` – `0x08007FFF` | 16 KB | **BootLoader** |
| Sector 2 | `0x08008000` – `0x0800BFFF` | 16 KB | **BootLoader** |
| Sector 3 | `0x0800C000` – `0x0800FFFF` | 16 KB | **BootLoader** |
| Sector 4 | `0x08010000` – `0x0801FFFF` | 64 KB | **Application** |
| Sector 5 | `0x08020000` – `0x0803FFFF` | 128 KB | **Application** |
| Sector 6 | `0x08040000` – `0x0805FFFF` | 128 KB | **Application** |
| Sector 7 | `0x08060000` – `0x0807FFFF` | 128 KB | **Application** |
| Sector 8 | `0x08080000` – `0x0809FFFF` | 128 KB | **Application** |
| Sector 9 | `0x080A0000` – `0x080BFFFF` | 128 KB | **Application** |
| Sector 10 | `0x080C0000` – `0x080DFFFF` | 128 KB | **Application** |
| Sector 11 | `0x080E0000` – `0x080FFFFF` | 128 KB | **Application** |

关键参数：

| 项目 | 值 |
|------|-----|
| **BootLoader 预留空间** | 64 KB（Sector 0–3） |
| **Application 可用空间** | 960 KB（Sector 4–11） |
| **App 起始地址** | `APP_START_ADDR = 0x08010000` |
| **App 最大体积** | `APP_MAX_SIZE = 0x000F0000`（960 KB） |

> **BootLoader 设计约束**：64 KB 裸机程序，不含 FreeRTOS、FATFS、LVGL。仅依赖 W25Q64 驱动（读固件暂存区/备份区）、内部 Flash HAL（擦写）、LCD 驱动（显示进度/菜单）、按键驱动（菜单交互）、CRC32。这 64 KB 对上述模块完全够用。

### 1.2 W25Q64 外部 SPI Flash 分区（8 MB）

| 分区 | 地址范围 | 大小 | 用途 |
|------|----------|------|------|
| **字体区** | `0x000000` – `0x1FFFFF` | 2 MB | XBF 字库（已有，`SYSTEM_FONT_FLASH_ADDR`） |
| **预留区** | `0x200000` – `0x5FFFFF` | 4 MB | 未来扩展（NES ROM 缓存等） |
| **固件暂存区** | `0x600000` – `0x6FFFFF` | 1 MB | SD 卡 `update.bin` 写入目标 |
| **固件备份区** | `0x700000` – `0x7FFFFF` | 1 MB | 当前运行固件的完整备份 |

关键常量定义建议：

```c
#define W25Q64_FW_STAGING_ADDR    (0x600000UL)   /* 6 MB */
#define W25Q64_FW_STAGING_SIZE    (0x100000UL)   /* 1 MB */
#define W25Q64_FW_BACKUP_ADDR     (0x700000UL)   /* 7 MB */
#define W25Q64_FW_BACKUP_SIZE     (0x100000UL)   /* 1 MB */
```

> 暂存区和备份区各 1 MB，与内部 Flash 总容量对齐。实际固件 ≤ 960 KB（Application 区），1 MB 足够容纳并留有裕量。

### 1.3 外部 SRAM 页缓冲区（1 MB @ 0x68000000）

| 项目 | 值 |
|------|-----|
| **页缓冲区大小** | `IAP_TRANSFER_BUFFER_SIZE = 4 KB`（`0x1000`） |
| **分配来源** | `SRAMEX` 内存池（`sys_malloc(SRAMEX, ...)`） |
| **生命周期** | 更新流程开始时分配，写入 W25Q64 完成后释放 |
| **并发约束** | 由 GUI 线程同步执行；启用 USB MSC 时禁止开始更新 |

> 当前实现使用固定 4 KB 缓冲，避免更新期间额外占用大块连续外部 SRAM，并与 FatFs/W25Q64
> 分页接口直接配合。BootLoader 使用 1 KB 静态片上 SRAM 缓冲，不依赖外部 SRAM。

### 1.4 AT24C02 EEPROM 数据布局（256 字节）

系统设置记录当前占用 36 字节（Offset 0–35）。BootLoader 使用两个独立校验的 36 字节槽：

| 偏移 | 大小 | 用途 |
|------|------|------|
| `0` | 36 B | `System_Settings_Record_t` |
| `128` | 36 B | BootLoader 状态槽 0（128–163） |
| `168` | 36 B | BootLoader 状态槽 1（168–203） |

每槽包含魔数 `0x424C4932`（"BLI2"）、版本、记录长度、单调序号、暂存大小/CRC、备份
大小/CRC、更新/恢复/备份状态和 FNV-1a 校验。保存时只覆盖较旧槽，写后立即回读验证；断电
撕裂写会因校验失败被丢弃，另一槽仍可用。完整字段见附录 B。

---

## 2. 更新流程

更新流程分为两大阶段：**APP 端预处理**（在 RTOS 环境下完成 SD 卡读取与 CRC 计算）和 **BootLoader 端烧写**（裸机环境完成内部 Flash 编程）。APP 成功提交双槽状态记录后调用 `NVIC_SystemReset()`，自动进入 BootLoader。

### 2.1 整体时序

```
[APP 正常运行]
    │
    ├── 用户触发更新（GUI 菜单 / Shell 命令）
    │
    ├─→ 阶段 A: APP 端预处理
    │       1. 检测 SD 卡 update.bin
    │       2. 分页读取 → 外部 SRAM 4KB 缓冲 → 写入 W25Q64 暂存区
    │       3. 比对 SD 源数据 CRC 与 W25Q64 回读 CRC
    │       4. 双槽提交 update_state = 0xAA
    │       5. GUI 显示完成状态并自动复位
    │
    ├── 用户重启设备
    │
    ├─→ 阶段 B: BootLoader 启动检查
    │       1. 读取 EEPROM 双槽 update_state
    │       2. 若 = 0xAA → 进入更新模式
    │       3. 若 ≠ 0xAA → 直接跳转 APP
    │
    ├─→ 阶段 C: BootLoader 烧写
    │       1. 从 W25Q64 暂存区读取固件，校验 CRC32
    │       2. 擦除内部 Flash Sector 4–11（Application 区）
    │       3. 分页写入内部 Flash
    │       4. 回读校验
    │       5. 清除更新状态，保留上一版有效备份
    │       6. 跳转 APP
    │
    └── APP 正常启动（新固件运行）
```

### 2.2 阶段 A 详述：APP 端预处理

#### Step A1: 检测 update.bin

```
路径: "0:/update.bin"（SD 卡根目录）
检查: f_stat() → 获取文件大小 → 验证 0 < size ≤ APP_MAX_SIZE (960KB)
```

若文件不存在或大小非法 → GUI 提示错误退出。

#### Step A2: 分配页缓冲区并分页传输

```
1. pbuf = sys_malloc(SRAMEX, IAP_TRANSFER_BUFFER_SIZE)  // 4 KB
   若分配失败 → 提示内存不足退出

2. f_open(&fil, "0:/update.bin", FA_READ)

3. 擦除 W25Q64 固件暂存区（0x600000 起 1MB）
   → 使用 W25Q64_BlockErase64K() 连续擦除 16 个 64KB 块

4. 循环传输:
   while (remaining > 0) {
       chunk = min(remaining, IAP_TRANSFER_BUFFER_SIZE);
       f_read(&fil, pbuf, chunk, &br);      // SD → 外部 SRAM
       source_crc = crc32_update(source_crc, pbuf, chunk);
       W25Q64_WriteData(addr, pbuf, chunk); // 外部 SRAM → W25Q64
       addr += chunk;
       remaining -= chunk;
       更新 GUI 进度条;
   }

5. f_close(&fil); sys_free(SRAMEX, pbuf);
```

#### Step A3: CRC32 计算

写入过程中计算 SD 源文件 CRC，并对 W25Q64 暂存区回读计算第二份 CRC；两者必须一致，确保
暂存内容与 `update.bin` 完全一致：

```
source_crc  = crc32_finish(source_crc)
staging_crc = crc32_w25q64(W25Q64_FW_STAGING_ADDR, fw_size)
if (source_crc != staging_crc) 取消更新
```

> CRC32 算法详情见第 4 章。

#### Step A4: 写入 EEPROM 更新信息

```
IAP_RecordLoad(&record);                 // 取序号最新的有效槽
record.staging_size  = fw_size;
record.staging_crc32 = staging_crc;
record.update_state  = IAP_UPDATE_READY; // 0xAA
record.restore_state = IAP_RESTORE_IDLE;
IAP_RecordSave(&record);                 // 写入较旧槽并回读验证
```

#### Step A5: 提示用户

```
设置页状态栏显示 "Update ready"
延时 500 ms 后调用 NVIC_SystemReset()
```

> GUI 已复用 `GUI_Settings.c` 中的 "Install Update" 占位项；USB MSC 开启时拒绝更新，避免
> FatFs 与主机同时访问 SD 卡。

### 2.3 阶段 C 详述：BootLoader 烧写

#### Step C1: CRC 校验暂存固件

```
从 EEPROM 双槽读取序号最新的有效 IAP_Record_t
验证魔数、版本、长度和 FNV-1a 校验 → 无有效记录时按正常启动处理
读取暂存区前 8 字节，独立验证 MSP 范围、Reset_Handler 的 APP 地址与 Thumb 位

crc_actual = crc32_w25q64(IAP_STAGING_ADDRESS, record.staging_size)
if (crc_actual != record.staging_crc32) {
    LCD 显示 "CRC 校验失败，更新取消"
    若尚未擦除 APP：清除更新状态并保留原 APP
    若处于 PROGRAMMING 且备份有效：自动恢复上一版备份
}
```

#### Step C2: 自动备份（可选但推荐）

在擦除 Application 区之前，将当前固件备份到 W25Q64 备份区：

```
1. 擦除 W25Q64 备份区（0x700000 起 1MB）
2. 从内部 Flash 0x08010000 读取 APP_MAX_SIZE → 分页写入 W25Q64 备份区
3. 计算备份 CRC32 → 双槽提交 backup_state = 0xCC、backup_size 和 backup_crc32
```

> 当前实现固定执行自动备份。由于 APP 镜像尚无可信的嵌入式长度元数据，备份范围为完整
> `APP_MAX_SIZE`（960 KB），并记录备份大小与 CRC。

#### Step C3: 擦除 Application 区

```
HAL_FLASH_Unlock();
for (sector = 4; sector <= 11; sector++) {
    HAL_FLASHEx_Erase(&erase_init, &sector_error);
    LCD 进度条更新;
}
```

#### Step C4: 分页写入新固件

```
写入粒度: 32-bit word（STM32F4 Flash 编程要求）
数据源: W25Q64 暂存区
目标: 内部 Flash 0x08010000

addr_src  = W25Q64_FW_STAGING_ADDR;
addr_dst  = APP_START_ADDR;
remaining = record.staging_size;

while (remaining > 0) {
    chunk = min(remaining, BOOT_TRANSFER_BUFFER_SIZE);  // 1 KB
    W25Q64_ReadData(addr_src, pbuf, chunk);
    // 32-bit 对齐写入
    for (i = 0; i < chunk; i += 4) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, addr_dst + i, *(u32*)(pbuf + i));
    }
    addr_src  += chunk;
    addr_dst  += chunk;
    remaining -= chunk;
    LCD 进度条更新;
}
HAL_FLASH_Lock();
```

#### Step C5: 回读校验

```
写完后从内部 Flash 回读全部固件数据，与 W25Q64 暂存区逐字节比对。
若不一致 → LCD 显示"烧写验证失败" → 进入恢复模式（提示用户选择恢复备份或重试）
若一致 → 继续
```

#### Step C6: 清理标志并跳转

```
清除 update_state (写入 0x00)
保留 backup_state = 0xCC（指向更新前的上一版完整备份）
跳转 APP：
    - 设置 MSP = *(u32*)APP_START_ADDR
    - 设置 VTOR = APP_START_ADDR
    - 跳转到 Reset_Handler = *(u32*)(APP_START_ADDR + 4)
```

---

## 3. BootLoader 功能

### 3.1 启动流程

```
上电 / 复位
    │
    ▼
BootLoader 入口 (0x08000000)
    │
    ├── 最小硬件初始化
    │     - 系统时钟 168 MHz（使用 HSI 或 HSE，与 APP 一致）
    │     - SPI2（W25Q64）
    │     - GPIO 位模拟 I2C（AT24C02）
    │     - SPI1 / LCD（ST7789）
    │     - 按键 GPIO
    │     - SysTick 1ms（用于超时和去抖）
    │
    ├── 读取 EEPROM 双槽 IAP_Record_t
    │
    ├── 检查 restore_state == 0xBB ?
    │     ├── 是 → 进入"恢复模式"，跳过菜单直接恢复
    │     └── 否 → 继续
    │
    ├── 检查 update_state == 0xAA / 0x55 ?
    │     ├── 是 → LCD 显示 "检测到新固件，正在升级..."
    │     │         自动执行更新流程（第 2.3 节）
    │     │         完成后跳转 APP
    │     └── 否 → 继续
    │
    ├── 检测菜单键 PB7 是否连续按住 30 ms
    │     ├── 按住 → 进入 BootLoader 主菜单
    │     └── 未按 → 检查 APP 是否有效
    │                 ├── SP 和 PC 合法 → 直接跳转 APP
    │                 └── 非法 → 进入 BootLoader 主菜单
    │
    └── BootLoader 主菜单
```

> **跳转 APP 有效性检查**: 读取 `0x08010000` 处的前两个 word —— MSP 是否在 SRAM 范围内（`0x20000000`–`0x20020000`），PC（Reset_Handler）是否在 Flash 范围内且 bit[0]=1（Thumb 模式）。不满足则说明无有效固件，留在 BootLoader 菜单。

### 3.2 BootLoader 主菜单

LCD 显示四级菜单，按键操作：

| 菜单项 | 图标/文字 | 功能 |
|--------|-----------|------|
| **1. 恢复原固件** | `[↑↓] Restore` | 从 W25Q64 备份区恢复固件到内部 Flash |
| **2. 更新固件** | `[↑↓] Update` | 从 W25Q64 暂存区烧写固件到内部 Flash |
| **3. 备份固件** | `[↑↓] Backup` | 将当前内部 Flash 固件备份到 W25Q64 备份区 |
| **4. 启动 APP** | `[↑↓] Boot APP` | 直接跳转 Application |

#### 按键映射

复用现有 8 键驱动的键位别名：

| 操作 | 按键 | 宏 |
|------|------|-----|
| 上移光标 | UP | `KEY_GAME_UP`（PB1） |
| 下移光标 | DOWN | `KEY_GAME_DOWN`（PA1） |
| 确认选择 | OK / START | `KEY_GAME_OK`（PE2） |
| 返回 / 取消 | SELECT | `KEY_GAME_SELECT`（PB7） |

> BootLoader 阶段不依赖 FreeRTOS 的消息队列，直接读取 GPIO；按下稳定 25–30 ms 后确认，
> 等待释放后才接受下一次操作。

#### 菜单交互流程

```
主菜单 (4 项)
    │
    ├── 选择 "1. 恢复原固件"
    │     ├── 检查 backup_state == 0xCC 并校验备份 CRC
    │     │     ├── 否 → LCD "无有效备份" → 返回
    │     │     └── 是 → 立即执行恢复（第 3.3 节）
    │     └── 恢复完成 → 短暂显示结果后跳转 APP
    │
    ├── 选择 "2. 更新固件"
    │     ├── 检查 update_state == 0xAA / 0x55 ?
    │     │     ├── 否 → LCD "无可用更新" → 返回
    │     │     └── 是 → 立即执行更新（第 2.3 节）
    │     └── 更新完成 → 短暂显示结果后跳转 APP
    │
    ├── 选择 "3. 备份固件"
    │     ├── 检查 APP 区域是否有有效固件（SP/PC 合法性检查）
    │     │     ├── 否 → LCD "无有效固件可备份" → 返回
    │     │     └── 是 → 立即备份完整 960 KB APP 区并写入 backup_state = 0xCC
    │     └── 备份完成 → "备份成功" → 返回主菜单
    │
    └── 选择 "4. 启动 APP"
          └── 直接跳转 APP（同 3.1 末尾流程）
```

### 3.3 恢复原固件流程

```
1. 检查 `backup_state == 0xCC`、大小范围和 W25Q64 备份 CRC
2. 提交 `restore_state = 0xBB`，确保断电后会重试
3. 擦除内部 Flash Sector 4–11（Application 区）
4. 从 W25Q64 备份区（0x700000）逐页读取 → 写入内部 Flash 0x08010000
5. 回读校验（内部 Flash vs W25Q64 备份区）
6. 清除恢复/更新状态，LCD 显示结果并跳转 APP
```

### 3.4 备份固件流程

```
1. 擦除 W25Q64 备份区（0x700000 起 1MB，16 × BlockErase64K）
2. 从内部 Flash 0x08010000 读取 APP 固件 → 写入 W25Q64 备份区
3. 回读校验 CRC32
4. 更新 EEPROM: backup_state = 0xCC，记录备份大小和 CRC32
5. LCD 显示结果 → 返回主菜单
```

### 3.5 LCD UI 设计原则

BootLoader 的 LCD 显示需**独立于 RTOS、LVGL 和主工程 LCD 驱动**，使用轮询 HAL SPI/GPIO 直接驱动 ST7789：

- 字体：使用内置 5×7 或 8×16 点阵字模（编译进 BootLoader），不依赖 XBF 字库
- 颜色：黑底白字为主，进度条用橙色/绿色
- 无需 LVGL：内置 ST7789 初始化、矩形填充和 5×7 字符绘制

---

## 4. CRC32 校验机制

### 4.1 算法选型

| 项目 | 值 |
|------|-----|
| **算法** | CRC-32/ISO-HDLC（IEEE 802.3） |
| **多项式** | `0x04C11DB7`（normal），`0xEDB88320`（reflected） |
| **初始值** | `0xFFFFFFFF` |
| **输出 XOR** | `0xFFFFFFFF` |
| **数据顺序** | LSB-first（reflected） |
| **参考实现** | zlib `crc32()`、Linux `crc32_le()`、本项目 16 项 nibble 查表实现 |

> 选用此算法是因为它是业界最广泛使用的 CRC32 变体，zlib、PKZIP、Ethernet 均采用，验证工具丰富。
> STM32F407 的 CRC 外设固定为非反射输入/输出，不能直接等价计算该字节流算法；APP 与
> BootLoader 虽均保留硬件 CRC 初始化，更新协议实际使用相同的软件 CRC，避免端序转换歧义。

### 4.2 软件实现（查表法，适合 BootLoader 64KB 限制）

```c
/* 16-entry reflected nibble table: 64 bytes */
static const u32 crc32_table[16] = {
    0x00000000, 0x1DB71064, 0x3B6E20C8, 0x26D930AC, /* ... */
};

u32 crc32_calculate(const u8 *data, u32 length)
{
    u32 crc = 0xFFFFFFFF;
    for (u32 i = 0; i < length; i++) {
        crc ^= data[i];
        crc = crc32_table[crc & 0x0F] ^ (crc >> 4);
        crc = crc32_table[crc & 0x0F] ^ (crc >> 4);
    }
    return crc ^ 0xFFFFFFFF;
}
```

> 16 项表仅占 64 字节，APP 与 BootLoader 使用相同参数和逐字节更新顺序。

### 4.3 对 W25Q64 区域计算 CRC32 的辅助函数

```c
u32 crc32_w25q64(u32 addr, u32 length)
{
    u32 crc = 0xFFFFFFFF;
    u8 buf[256];  /* 栈上小缓冲，每次读 256 字节 */

    while (length > 0) {
        u32 chunk = (length > sizeof(buf)) ? sizeof(buf) : length;
        W25Q64_ReadData(addr, buf, chunk);
        for (u32 i = 0; i < chunk; i++) {
            crc = crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
        }
        addr   += chunk;
        length -= chunk;
    }
    return crc ^ 0xFFFFFFFF;
}
```

### 4.4 校验时机汇总

| 序号 | 时机 | 计算对象 | 对比对象 | 失败处理 |
|------|------|----------|----------|----------|
| ① | APP 写入 W25Q64 暂存区后 | W25Q64 `[0x600000, +size)` | SD 源文件流式 CRC | 放弃提交更新状态 |
| ② | BootLoader 烧写前 | W25Q64 暂存区回读 | EEPROM 中的 `staging_crc32` | 未擦除时取消；烧写中恢复备份 |
| ③ | BootLoader 烧写后 | 内部 Flash `[0x08010000, +size)` | W25Q64 暂存区逐字节比对 | 标记损坏、提示恢复 |
| ④ | 恢复原固件后 | 内部 Flash 回读 | W25Q64 备份区 | 保留恢复状态，重启后可重试 |
| ⑤ | 备份固件后 | W25Q64 备份区回读 | 内部 Flash 源数据 | 不提交有效备份状态 |

---

## 5. 安全与容错设计

### 5.1 断电保护策略

Flash 写入过程中断电是最危险的场景。采用**标志位驱动的状态机**来保证恢复能力：

```
EEPROM 状态字段组合含义：

update_state = 0x00, restore_state = 0x00
    → 正常状态，无需操作

update_state = 0xAA, restore_state = 0x00
    → 有新固件在暂存区，待烧写

update_state = 0x00, restore_state = 0xBB
    → 需要恢复备份（上次更新可能失败）

update_state = 0x55, restore_state = 0x00
    → 烧写进行中（断电后 BootLoader 检测到此值 → 重新执行烧写）
```

**关键规则：先写标志，后执行操作；操作完成后才清除标志。**

烧写流程的原子步骤：

```
Step 1: 备份完整旧 APP 并提交 backup_state = 0xCC、备份大小和 CRC
Step 2: 提交 update_state = 0x55  // "烧写进行中"
Step 3: 擦除内部 Flash Sector 4–11
Step 4: 写入新固件 + 回读校验
Step 5: 提交 update_state = 0x00  // "完成"，上一版备份继续有效
```

若在 Step 3/4 期间断电，重启后 BootLoader 检测到 `update_state = 0x55`，会重新校验暂存区并从擦除开始重试。若暂存区已损坏且备份有效，则提交恢复状态并自动恢复上一版。

### 5.2 双备份兜底

```
                  ┌─────────────────────┐
                  │     内部 Flash       │
                  │  APP 区 (Sector4-11) │
                  └─────────┬───────────┘
                            │
              ┌─────────────┴─────────────┐
              │                           │
    备份 ┌────▼─────┐            暂存 ┌───▼──────┐
    ←─── │ W25Q64   │           ←─── │ W25Q64   │
         │ 备份区   │                 │ 暂存区   │
         │ 7-8 MB   │                 │ 6-7 MB   │
         └──────────┘                 └──────────┘

- 备份区始终保存上一版"已知良好"的固件
- 暂存区保存待更新的新固件
- 即使暂存区数据损坏，仍可通过备份区恢复到上一版本
- 用户手动备份操作可在任意时刻更新备份区内容
```

### 5.3 跳转安全

从 BootLoader 跳转到 APP 前，必须：

1. **禁用所有中断**：`__disable_irq()`
2. **恢复 MSP**：`__set_MSP(*(u32*)APP_START_ADDR)`
3. **恢复 VTOR**：`SCB->VTOR = APP_START_ADDR`
4. **清零所有外设时钟**（可选但推荐）：避免 APP 初始化时遇到外设处于未知状态
5. **跳转到 Reset_Handler**：通过函数指针跳转，确保使用 `__attribute__((naked))` 包裹的跳转函数

```c
__attribute__((naked)) void bootloader_jump_to_app(u32 app_addr)
{
    u32 msp = *(u32 *)app_addr;
    u32 pc  = *(u32 *)(app_addr + 4);

    __set_MSP(msp);
    SCB->VTOR = app_addr;

    __asm volatile (
        "bx %0"
        :
        : "r" (pc)
        : "memory"
    );
}
```

### 5.4 看门狗

当前 APP 和 BootLoader 均未启用 IWDG。BootLoader 更新期间不应新启用独立看门狗，因为：

- Flash 擦除操作可能耗时较长（Sector 4–11 全部擦除约需 4–8 秒）
- IWDG 一旦启动不能由软件关闭，且系统复位后仍可能继续运行

若将来 APP 启用 IWDG，必须在 BootLoader 每个长操作内按实际超时配置持续喂狗，并做专门断电测试：

```c
// 每个 Sector 擦除后喂狗
// 每写入 128KB 后喂狗
IWDG->KR = 0xAAAA;  // Reload
```

### 5.5 异常处理矩阵

| 异常场景 | 检测方式 | 处理策略 |
|----------|----------|----------|
| update.bin 不存在 | `f_stat()` 返回错误 | GUI 提示"未找到 update.bin" |
| update.bin 过大 | 文件大小 > `APP_MAX_SIZE` | GUI 提示"固件超出限制" |
| SD 卡读取失败 | `f_read()` 返回错误/短读 | 中止，不提交更新状态 |
| W25Q64 写入失败 | `W25Q64_WriteData()` 返回错误 | 中止，不提交更新状态 |
| EEPROM 读写失败 | 双槽均不可读或写后校验失败 | 保留旧槽，放弃本次状态转换 |
| 暂存 CRC32 校验失败 | 源/回读或 EEPROM/回读不一致 | APP 未擦除时取消；烧写中则自动恢复有效备份 |
| 烧写中途断电 | `update_state = 0x55` | 重新校验并从擦除开始烧写 |
| 内部 Flash 擦除失败 | `HAL_FLASHEx_Erase()` 返回错误 | 立即锁 Flash；有有效备份则进入恢复流程 |
| 内部 Flash 编程失败 | `HAL_FLASH_Program()` 返回错误 | 立即锁 Flash；有有效备份则进入恢复流程 |
| APP 区域无有效固件 | SP/PC 合法性检查失败 | 留在 BootLoader 菜单，不自动跳转 |

---

## 6. 实施清单

软件实施项已完成；最后一项必须在目标板上执行。

| 序号 | 状态 | 任务 | 涉及文件 |
|------|------|------|----------|
| 1 | 完成 | 分区与双槽共享协议 | `IAP/IAP_Protocol.h` |
| 2 | 完成 | APP/BootLoader 软件 CRC32 | `IAP/IAP.c`、`Boot/Src/boot_crc.c` |
| 3 | 完成 | APP 端 SD→W25Q64→双 CRC→EEPROM | `IAP/IAP.c/h` |
| 4 | 完成 | AT24C02 双槽掉电安全读写 | APP IAP 与 `Boot/Src/boot_storage.c` |
| 5 | 完成 | 64 KB 裸机 BootLoader 工程 | `BootLoader/StarGamerBootLoader/` |
| 6 | 完成 | ST7789 菜单、进度与按键 | `Boot/Src/boot_ui.c` |
| 7 | 完成 | 更新、完整备份、恢复、断点状态机与安全跳转 | `Boot/Src/bootloader.c` |
| 8 | 完成 | APP 链接到 `0x08010000` | `System/StarGamer_CCM.sct` |
| 9 | 完成 | APP VTOR 设置和 Keil 工程地址 | `Core/Src/main.c`、`MDK-ARM/*.uvprojx` |
| 10 | 完成 | 复用设置页 "Install Update" 占位项 | `GUI/GUI_Settings.c` |
| 11 | 待板测 | 更新/恢复/跳转及各 EEPROM/Flash 提交点断电测试 | 目标硬件 |

---

## 附录 A：关键常量速查

```c
/* ---- 内部 Flash 分区 ---- */
#define APP_START_ADDR              0x08010000UL
#define APP_MAX_SIZE                0x000F0000UL   /* 960 KB */
#define BOOTLOADER_START_ADDR       0x08000000UL
#define BOOTLOADER_MAX_SIZE         0x00010000UL   /* 64 KB */

/* ---- W25Q64 分区 ---- */
#define W25Q64_FONT_ADDR            0x000000UL     /* 0 MB, 已有 */
#define W25Q64_FONT_SIZE            0x200000UL     /* 2 MB */
#define W25Q64_RESERVED_ADDR        0x200000UL     /* 2 MB */
#define W25Q64_RESERVED_SIZE        0x400000UL     /* 4 MB */
#define W25Q64_FW_STAGING_ADDR      0x600000UL     /* 6 MB */
#define W25Q64_FW_STAGING_SIZE      0x100000UL     /* 1 MB */
#define W25Q64_FW_BACKUP_ADDR       0x700000UL     /* 7 MB */
#define W25Q64_FW_BACKUP_SIZE       0x100000UL     /* 1 MB */

/* ---- 外部 SRAM ---- */
#define IAP_TRANSFER_BUFFER_SIZE     (4UL * 1024UL)    /* APP staging */
#define BOOT_TRANSFER_BUFFER_SIZE    1024UL            /* BootLoader static buffer */

/* ---- EEPROM ---- */
#define IAP_EEPROM_SLOT0_ADDRESS    128
#define IAP_EEPROM_SLOT1_ADDRESS    168
#define IAP_RECORD_SIZE             36

/* ---- BootLoader 魔数 ---- */
#define BL_MAGIC                    0x424C4932UL  /* "BLI2" */
#define BL_UPDATE_FLAG_READY        0xAA
#define BL_UPDATE_FLAG_INPROGRESS   0x55
#define BL_RESTORE_FLAG             0xBB
#define BL_BACKUP_VALID             0xCC

/* ---- App 有效性检查 ---- */
#define SRAM_START                  0x20000000UL
#define SRAM_END                    0x20020000UL   /* 128 KB */
#define FLASH_START                 0x08000000UL
#define FLASH_END                   0x08100000UL
#define THUMB_BIT_MASK              0x00000001UL
```

## 附录 B：IAP_Record_t / Boot_Record_t 结构体

```c
typedef struct IAP_Record {
    u32 magic;            /* 0x424C4932 "BLI2" */
    u16 version;          /* 当前为 2 */
    u16 record_size;      /* 36 */
    u32 sequence;         /* 双槽新旧判定，支持回绕比较 */
    u32 staging_size;     /* 暂存固件大小 */
    u32 staging_crc32;    /* 暂存固件 CRC32 */
    u32 backup_size;      /* 当前固定为 0x000F0000 */
    u32 backup_crc32;     /* 上一版完整 APP 区 CRC32 */
    u8  update_state;     /* 0xAA=待更新, 0x55=烧写中, 0x00=空闲 */
    u8  restore_state;    /* 0xBB=需恢复备份, 0x00=空闲 */
    u8  backup_state;     /* 0xCC=有效备份, 0x00=无备份 */
    u8  reserved;
    u32 checksum;         /* 前 32 字节的 FNV-1a */
} IAP_Record_t;
```

## 附录 C：现有代码中已预留的 IAP 相关字段

`System/System_Status.h` 中保留了早期规划字段，但当前持久化协议不依赖这些运行时字段：

```c
/* System_Core_Information_t (运行时) */
SCI.AvilableUpdateReady
SCI.NeedRecovery
SCI.isSystemBackup
SCI.AppBinLength

/* IAP_Info_t（遗留运行时结构） */
IAP_Info.NeedBackUpSystem
IAP_Info.AvilableUpdateReady
IAP_Info.NeedRecovery
IAP_Info.AppBinLength
```

> APP 与 BootLoader 的唯一持久化契约是 `IAP/IAP_Protocol.h`。如后续需要在 UI 查询状态，
> 应通过 `IAP_RecordLoad()` 映射到运行时展示，避免再定义一套 EEPROM 布局。

---

> **文档结束** — 软件实现与编译验证已完成；第 6 章中的目标板测试仍为发布前必做项。
