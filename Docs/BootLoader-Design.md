# Star Gamer — BootLoader 固件更新方案设计

> **目标平台**: STM32F407ZGT6（Cortex-M4F @ 168 MHz，1 MB 内部 Flash，192 KB 内部 SRAM + 1 MB 外部 SRAM）
> **设计原则**: 最小侵入、断电容错、双备份兜底、CRC 全程校验
> **版本**: v1.0
> **状态**: 设计阶段 — 本文档为纯方案描述，尚未实施代码修改

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
| **页缓冲区大小** | `PAGE_BUFFER_SIZE = 256 KB`（`0x40000`） |
| **分配来源** | `SRAMEX` 内存池（`mymalloc(SRAMEX, ...)`） |
| **生命周期** | 更新流程开始时分配，写入 W25Q64 完成后释放 |
| **并发约束** | 更新期间暂停 LVGL 渲染（帧缓冲可暂时释放以腾出空间） |

> 256 KB 是权衡后的值：足够大以减少 SD 卡读取次数（约 4 次读完 960 KB 固件），同时不会挤占外部 SRAM 上其他关键数据。若外部 SRAM 碎片化严重，可降级使用 128 KB 或 64 KB 页缓冲。

### 1.4 AT24C02 EEPROM 数据布局（256 字节）

当前 EEPROM 仅存储 `System_Settings_Record_t`（Offset 0），占用约 120 字节。BootLoader 信息区紧接其后：

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| `0` | ~120 B | `System_Settings_Record_t` | 已有，系统设置持久化 |
| `128` | 4 B | `bl_magic` | BootLoader 魔数 `0x424C4942`（"BLIB"） |
| `132` | 4 B | `bl_fw_size` | 暂存固件大小（字节） |
| `136` | 4 B | `bl_fw_crc32` | 暂存固件 CRC32 校验值 |
| `140` | 1 B | `bl_update_flag` | 更新标志：`0xAA`=有新固件待更新 |
| `141` | 1 B | `bl_restore_flag` | 恢复标志：`0xBB`=需恢复备份 |
| `142` | 1 B | `bl_backup_flag` | 备份状态：`0x00`=无备份，`0xCC`=有效备份 |
| `143` | 1 B | `bl_reserved` | 保留 |
| `144` | 4 B | `bl_checksum` | 以上 16 字节的简单校验和（32-bit 累加） |

> EEPROM 总容量 256 字节，BootLoader 信息区占用 20 字节（128–147），总使用量 ≤ 148 字节，余量充分。

---

## 2. 更新流程

更新流程分为两大阶段：**APP 端预处理**（在 RTOS 环境下完成 SD 卡读取与 CRC 计算）和 **BootLoader 端烧写**（裸机环境完成内部 Flash 编程）。两个阶段之间通过**用户手动重启**衔接。

### 2.1 整体时序

```
[APP 正常运行]
    │
    ├── 用户触发更新（GUI 菜单 / Shell 命令）
    │
    ├─→ 阶段 A: APP 端预处理
    │       1. 检测 SD 卡 update.bin
    │       2. 分页读取 → 外部 SRAM 256KB 缓冲 → 写入 W25Q64 暂存区
    │       3. 计算 CRC32 并写入 EEPROM
    │       4. 设置 bl_update_flag = 0xAA
    │       5. GUI 提示"更新已就绪，请重启以完成升级"
    │
    ├── 用户重启设备
    │
    ├─→ 阶段 B: BootLoader 启动检查
    │       1. 读取 EEPROM bl_update_flag
    │       2. 若 = 0xAA → 进入更新模式
    │       3. 若 ≠ 0xAA → 直接跳转 APP
    │
    ├─→ 阶段 C: BootLoader 烧写
    │       1. 从 W25Q64 暂存区读取固件，校验 CRC32
    │       2. 擦除内部 Flash Sector 4–11（Application 区）
    │       3. 分页写入内部 Flash
    │       4. 回读校验
    │       5. 清除 bl_update_flag，写入 bl_backup_flag = 0xCC（新固件生效即视为有效备份）
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
1. pbuf = mymalloc(SRAMEX, PAGE_BUFFER_SIZE)  // 256 KB
   若分配失败 → 降级尝试 128KB / 64KB
   若仍失败 → 提示内存不足退出

2. f_open(&fil, "0:/update.bin", FA_READ)

3. 擦除 W25Q64 固件暂存区（0x600000 起 1MB）
   → 使用 W25Q64_BlockErase64K() 连续擦除 16 个 64KB 块

4. 循环传输:
   while (remaining > 0) {
       chunk = min(remaining, PAGE_BUFFER_SIZE);
       f_read(&fil, pbuf, chunk, &br);      // SD → 外部 SRAM
       W25Q64_WriteData(addr, pbuf, chunk); // 外部 SRAM → W25Q64
       addr += chunk;
       remaining -= chunk;
       更新 GUI 进度条;
   }

5. f_close(&fil); myfree(SRAMEX, pbuf);
```

#### Step A3: CRC32 计算

对 W25Q64 暂存区中刚写入的固件数据 **回读计算 CRC32**（非对 SD 卡源文件计算），确保写入过程无位错误：

```
crc = crc32_w25q64(W25Q64_FW_STAGING_ADDR, fw_size)
```

> CRC32 算法详情见第 4 章。

#### Step A4: 写入 EEPROM 更新信息

```
AT24C02_Write(128, &bl_info, sizeof(BootLoader_Info_t))
// bl_info.bl_magic       = 0x424C4942
// bl_info.bl_fw_size     = fw_size
// bl_info.bl_fw_crc32    = crc
// bl_info.bl_update_flag = 0xAA
// bl_info.bl_backup_flag = 不变（保留已有备份状态）
// bl_info.bl_checksum    = 计算前 16 字节累加和
```

#### Step A5: 提示用户

```
GUI 弹窗: "固件已就绪，请重启设备以完成升级"
          "如 30 秒内未重启，可选择稍后手动重启"
```

> GUI 实现细节：新增 `GUI_PAGE_BOOTLOADER_PROMPT` 页面，或复用现有设置页面的对话框组件。

### 2.3 阶段 C 详述：BootLoader 烧写

#### Step C1: CRC 校验暂存固件

```
从 EEPROM 读取 BootLoader_Info_t
验证 bl_checksum → 若失败则放弃更新、跳转 APP

crc_actual = crc32_w25q64(W25Q64_FW_STAGING_ADDR, bl_info.bl_fw_size)
if (crc_actual != bl_info.bl_fw_crc32) {
    LCD 显示 "CRC 校验失败，更新取消"
    清除 bl_update_flag
    跳转 APP
}
```

#### Step C2: 自动备份（可选但推荐）

在擦除 Application 区之前，将当前固件备份到 W25Q64 备份区：

```
1. 擦除 W25Q64 备份区（0x700000 起 1MB）
2. 从内部 Flash 0x08010000 读取 APP_MAX_SIZE → 分页写入 W25Q64 备份区
3. 计算备份 CRC32 → 写入 EEPROM bl_backup_flag = 0xCC
```

> 若空间紧张可跳过自动备份，但**强烈建议保留**——这是用户"恢复原固件"的唯一数据来源。

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
remaining = bl_info.bl_fw_size;

while (remaining > 0) {
    chunk = min(remaining, PAGE_BUFFER_SIZE);
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
清除 bl_update_flag (写入 0x00)
设置 bl_backup_flag = 0xCC（标记已有有效备份）
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
    ├── 读取 EEPROM BootLoader_Info_t
    │
    ├── 检查 bl_restore_flag == 0xBB ?
    │     ├── 是 → 进入"恢复模式"，跳过菜单直接恢复
    │     └── 否 → 继续
    │
    ├── 检查 bl_update_flag == 0xAA ?
    │     ├── 是 → LCD 显示 "检测到新固件，正在升级..."
    │     │         自动执行更新流程（第 2.3 节）
    │     │         完成后跳转 APP
    │     └── 否 → 继续
    │
    ├── 检测"菜单键"是否被按住（例如 KEY_GAME_SELECT，超时 2 秒）
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

> BootLoader 阶段不依赖 FreeRTOS 的消息队列，直接轮询 `KEY_GetState()` 读取键值，软件去抖（30ms 间隔采样）。

#### 菜单交互流程

```
主菜单 (4 项)
    │
    ├── 选择 "1. 恢复原固件"
    │     ├── 检查 bl_backup_flag == 0xCC ?
    │     │     ├── 否 → LCD "无有效备份" → 返回
    │     │     └── 是 → LCD 确认: "确定恢复原固件？[OK]确认 [SELECT]取消"
    │     │             ├── OK → 执行恢复（第 3.3 节）
    │     │             └── SELECT → 返回
    │     └── 恢复完成 → "恢复成功，按任意键重启" → 跳转 APP
    │
    ├── 选择 "2. 更新固件"
    │     ├── 检查 bl_update_flag == 0xAA ?
    │     │     ├── 否 → LCD "无可用更新" → 返回
    │     │     └── 是 → LCD 确认: "确定更新固件？[OK]确认 [SELECT]取消"
    │     │             ├── OK → 执行更新（第 2.3 节）
    │     │             └── SELECT → 返回
    │     └── 更新完成 → "更新成功，按任意键重启" → 跳转 APP
    │
    ├── 选择 "3. 备份固件"
    │     ├── 检查 APP 区域是否有有效固件（SP/PC 合法性检查）
    │     │     ├── 否 → LCD "无有效固件可备份" → 返回
    │     │     └── 是 → LCD 确认: "确定备份当前固件？[OK]确认"
    │     │             ├── OK → 执行备份（擦除 W25Q64 备份区 → 从内部 Flash 读取 → 写入备份区）
    │     │             │         写入 bl_backup_flag = 0xCC
    │     │             └── SELECT → 返回
    │     └── 备份完成 → "备份成功" → 返回主菜单
    │
    └── 选择 "4. 启动 APP"
          └── 直接跳转 APP（同 3.1 末尾流程）
```

### 3.3 恢复原固件流程

```
1. 从 EEPROM 读取备份信息（bl_backup_flag 必须 = 0xCC）
2. 擦除内部 Flash Sector 4–11（Application 区）
3. 从 W25Q64 备份区（0x700000）逐页读取 → 写入内部 Flash 0x08010000
4. 回读校验（内部 Flash vs W25Q64 备份区）
5. 清除 bl_restore_flag（若存在）
6. LCD 显示结果
7. 跳转 APP
```

### 3.4 备份固件流程

```
1. 擦除 W25Q64 备份区（0x700000 起 1MB，16 × BlockErase64K）
2. 从内部 Flash 0x08010000 读取 APP 固件 → 写入 W25Q64 备份区
3. 回读校验 CRC32
4. 更新 EEPROM: bl_backup_flag = 0xCC, 记录备份 CRC32
5. LCD 显示结果 → 返回主菜单
```

### 3.5 LCD UI 设计原则

BootLoader 的 LCD 显示需**独立于 HAL 和 LVGL**，直接驱动 ST7789：

- 字体：使用内置 5×7 或 8×16 点阵字模（编译进 BootLoader），不依赖 XBF 字库
- 颜色：黑底白字为主，进度条用蓝色/绿色
- 无需 LVGL：直接调用 `LCD_ShowString()`、`LCD_Fill()` 等底层函数
- 参考现有 HardFault LCD 显示逻辑（`firmware_start.c` 中 `hardfault_lcd_show()`）——该函数已能在无 HAL/RTOS 环境下工作

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
| **参考实现** | zlib `crc32()`、Linux `crc32_le()`、STM32 CRC 外设（可配置为兼容模式） |

> 选用此算法是因为它是业界最广泛使用的 CRC32 变体，zlib、PKZIP、Ethernet 均采用，验证工具丰富。

### 4.2 软件实现（查表法，适合 BootLoader 64KB 限制）

```c
/* CRC-32 lookup table (256 × 4 bytes = 1 KB) */
static const u32 crc32_table[256] = {
    0x00000000, 0x77073096, 0xEE0E612C, 0x990951BA, /* ... 共 256 项 */
};

u32 crc32_calculate(const u8 *data, u32 length)
{
    u32 crc = 0xFFFFFFFF;
    for (u32 i = 0; i < length; i++) {
        crc = crc32_table[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;
}
```

> 查表占用 1 KB Flash（BootLoader 64 KB 内完全可接受）。若需极致压缩，可运行时动态生成表（约 100 字节代码，无需 1 KB 表），但 STM32F407 Flash 充足，查表法更简单可靠。

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
| ① | APP 写入 W25Q64 暂存区后 | W25Q64 `[0x600000, +size)` | —（记录到 EEPROM） | 重试写入 |
| ② | BootLoader 烧写前 | W25Q64 暂存区回读 | EEPROM 中的 `bl_fw_crc32` | 放弃更新、清除标志 |
| ③ | BootLoader 烧写后 | 内部 Flash `[0x08010000, +size)` | W25Q64 暂存区逐字节比对 | 标记损坏、提示恢复 |
| ④ | 恢复原固件后 | 内部 Flash 回读 | W25Q64 备份区 | 重试恢复 |
| ⑤ | 备份固件后 | W25Q64 备份区回读 | 内部 Flash 源数据 | 重试备份 |

---

## 5. 安全与容错设计

### 5.1 断电保护策略

Flash 写入过程中断电是最危险的场景。采用**标志位驱动的状态机**来保证恢复能力：

```
EEPROM 标志位组合含义：

bl_update_flag = 0x00, bl_restore_flag = 0x00
    → 正常状态，无需操作

bl_update_flag = 0xAA, bl_restore_flag = 0x00
    → 有新固件在暂存区，待烧写

bl_update_flag = 0x00, bl_restore_flag = 0xBB
    → 需要恢复备份（上次更新可能失败）

bl_update_flag = 0x55, ... (任何)
    → 烧写进行中（断电后 BootLoader 检测到此值 → 重新执行烧写）
```

**关键规则：先写标志，后执行操作；操作完成后才清除标志。**

烧写流程的原子步骤：

```
Step 1: 写入 bl_update_flag = 0x55  // "烧写进行中"
Step 2: 擦除内部 Flash Sector 4–11
Step 3: 写入新固件 + 回读校验
Step 4: 写入 bl_update_flag = 0x00  // "完成"
        写入 bl_backup_flag = 0xCC  // 标记有效备份
```

若在 Step 2/3 期间断电，重启后 BootLoader 检测到 `bl_update_flag = 0x55` → 判定上次烧写中断 → 自动从 Step 2 重新开始（因为 Flash 可能处于部分擦除状态，必须重新擦除再写）。

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

BootLoader 整个运行期间**不应使能独立看门狗（IWDG）**，因为：

- Flash 擦除操作可能耗时较长（Sector 4–11 全部擦除约需 4–8 秒）
- 若 APP 在启动时使能了 IWDG，BootLoader 应在进入时立即禁用或重新配置

若必须使用看门狗，建议：

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
| SD 卡读取失败 | `f_read()` 返回错误 | 重试 3 次，仍失败则中止 |
| W25Q64 写入失败 | `W25Q64_WriteData()` 返回错误 | 重试 3 次，标记暂存区无效 |
| EEPROM 读写失败 | AT24C02 函数返回错误 | 重试 3 次，放弃本次操作 |
| CRC32 校验失败 | 比对不一致 | 清除更新标志，提示用户 |
| 烧写中途断电 | `bl_update_flag = 0x55` | 重新执行烧写 |
| 内部 Flash 擦除失败 | `HAL_FLASHEx_Erase()` 返回错误 | 标记扇区损坏，尝试跳过（若为后部扇区） |
| 内部 Flash 编程失败 | `HAL_FLASH_Program()` 返回错误 | 重试 3 次，仍失败则进入恢复流程 |
| APP 区域无有效固件 | SP/PC 合法性检查失败 | 留在 BootLoader 菜单，不自动跳转 |

---

## 6. 实施清单

下表按依赖关系排列，可作为开发路线图。**本文档批准后，后续按此清单逐步实施。**

| 序号 | 任务 | 涉及文件 | 说明 |
|------|------|----------|------|
| 1 | 定义分区宏和 BootLoader_Info_t 结构体 | `System/System_Status.h` | 新增 W25Q64 分区地址、EEPROM 布局偏移、`BootLoader_Info_t` 类型 |
| 2 | 实现 CRC32 模块 | `System/crc32.c/h` | 查表法 CRC32，提供 `crc32_calculate()` 和 `crc32_w25q64()` |
| 3 | 实现 APP 端更新预处理 | `IAP/IAP.c/h` | `IAP_PrepareUpdate()` —— SD→W25Q64→CRC→EEPROM |
| 4 | 扩展 EEPROM 读写 | `System/System_Status.c` | 增加 `BootLoader_Info_Save/Load()` 函数 |
| 5 | 创建 BootLoader 独立工程 | `BootLoader/` | 基于裸机，复用 W25Q64/LCD/KEY 驱动，无 RTOS |
| 6 | 编写 BootLoader LCD UI | `BootLoader/bootloader_ui.c` | 菜单显示、进度条、按键交互 |
| 7 | 实现 BootLoader 核心逻辑 | `BootLoader/bootloader_main.c` | 启动检查、更新、恢复、备份、跳转 |
| 8 | 修改链接脚本（APP 端） | `System/StarGamer_CCM.sct` | ROM 起始地址改为 `0x08010000`，长度 `0x000F0000` |
| 9 | 修改 main.c VTOR 设置 | `Core/Src/main.c` | `SCB->VTOR = 0x08010000`（APP 工程） |
| 10 | 新增 GUI 更新触发页面 | `GUI/GUI_BootLoader.c/h` | 触发更新、进度显示、重启提示 |
| 11 | 集成测试 | — | 备份→更新→恢复全流程测试，断电测试 |

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
#define PAGE_BUFFER_SIZE            (256UL * 1024UL)  /* 256 KB */

/* ---- EEPROM ---- */
#define EEPROM_BOOTLOADER_OFFSET    128
#define BOOTLOADER_INFO_SIZE        20

/* ---- BootLoader 魔数 ---- */
#define BL_MAGIC                    0x424C4942UL  /* "BLIB" */
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

## 附录 B：BootLoader_Info_t 结构体

```c
typedef struct BootLoader_Info {
    u32 magic;            /* 0x424C4942 "BLIB" */
    u32 fw_size;          /* 暂存固件大小（字节） */
    u32 fw_crc32;         /* 暂存固件 CRC32 */
    u8  update_flag;      /* 0xAA=待更新, 0x55=烧写中, 0x00=空闲 */
    u8  restore_flag;     /* 0xBB=需恢复备份, 0x00=空闲 */
    u8  backup_flag;      /* 0xCC=有效备份, 0x00=无备份 */
    u8  reserved;
    u32 checksum;         /* 前 16 字节的 32-bit 累加和 */
} BootLoader_Info_t;
```

## 附录 C：现有代码中已预留的 IAP 相关字段

`System/System_Status.h` 中已定义但未使用的字段可直接复用：

```c
/* System_Core_Information_t (运行时) */
SCI.AvilableUpdateReady   // → 可映射到 bl_update_flag == 0xAA
SCI.NeedRecovery          // → 可映射到 bl_restore_flag == 0xBB
SCI.isSystemBackup        // → 可映射到 bl_backup_flag == 0xCC
SCI.AppBinLength          // → 可映射到 bl_fw_size

/* IAP_Info_t (已定义结构体，IAP.c 为空) */
IAP_Info.NeedBackUpSystem
IAP_Info.AvilableUpdateReady
IAP_Info.NeedRecovery
IAP_Info.AppBinLength
```

> 建议：在实施阶段将 `System_Core_Information_t` 中的 IAP 字段与 EEPROM 中的 `BootLoader_Info_t` 同步，使 APP 层能通过 `SCI` 全局变量查询 BootLoader 状态。

---

> **文档结束** — 本文档为方案设计，未改动任何代码。实施时请依次执行第 6 章的实施清单。
