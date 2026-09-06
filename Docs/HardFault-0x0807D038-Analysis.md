# Confirm / Yes-No 压力测试后的焦点遍历 HardFault

分析日期：2026-09-06。此次仅增加分析文档和离线脚本，未修改固件实现。

## 结论

根因是弹窗按钮跨输入组移动时，旧组中的对象引用未被移除，导致弹窗销毁后出现 use-after-free。

`lv_btn_create()` 自动把按钮加入默认组；`GUI_Message_DialogCreate()` 随后调用 `lv_group_add_obj()` 将同一按钮加入弹窗组。但工程所用 LVGL 8.2.0 的这个函数只修改按钮的 `group_p` 并向目标组追加节点，没有删除旧组的节点。弹窗关闭仅能清理当前所属的弹窗组，Settings 默认组仍引用已经释放的按钮。

本次 DUMP 的默认组和全局组链表的双向链接均完整，损坏的是默认组节点引用的对象生命周期。将弹窗组改成永久存在，无法消除这类悬空对象引用。

## 分析输入与一致性

| 输入 | 路径 | SHA-256 |
| --- | --- | --- |
| 主固件 AXF | `MDK-ARM/Star Gamer/Star Gamer.axf` | `C45AE6FD3EA08F0C3A0D3890B8C8C383DDF5691C3CE75016975ECECC10778E7A` |
| DUMP | `HardFault -dump/hardfault_dump-1.1.4-0x0807D038.bin` | `125951BB637C4AE7B7D3C289BDFD6D62988A2C24640EDB8D0A1FB90B8A31522F` |
| BootLoader AXF | `BootLoader/StarGamerBootLoader/MDK-ARM/StarGamerBootLoader/StarGamerBootLoader.axf` | `EFADDCFC890D2F038058EF189551669F81DD11D9E038B07C99DA84C50B25964D` |

DUMP 为 SDMP v1，160 字节头，256 字节数据偏移，含全部 CCM、主 SRAM、外部 SRAM。文件长度 1,245,440 字节。头部 FNV-1a `0x1727F7FA`、载荷 FNV-1a `0x0DD65F93` 均通过校验；PSP 硬件异常栈帧与头部寄存器记录一致。

用户文字记录的 R0/BFAR 是 `0x7EEE7F00 / 0x7EEE7F1C`；提供的 DUMP 实际记录的是 `0x2D000047 / 0x2D000063`。两次的 PC、LR、PSP、CFSR 和固定寄存器一致，属于相同故障位置的不同现场。下面的具体内存地址链针对 DUMP。

另一个 `hardfault_dump-1.1.4-0x0804F5BC-PSP-HFSR-BUS.bin` 对应更早现场。当前 AXF 将该 PC 映射到不同代码，不能用当前地址表直接给它定因，也不能据此认定它是 JPEG 故障。

## PC / LR 与异常类型

| 地址或寄存器 | 定位 / 含义 |
| --- | --- |
| PC `0x0807D038` | `lv_obj.c:298`，`lv_obj_has_flag()`，指令 `ldr r0, [r0, #28]` |
| LR `0x08053E51` | Thumb 返回地址，对应 `lv_group.c:411`，`focus_next_core()` 检查对象及父对象是否隐藏 |
| R1 `1` | `LV_OBJ_FLAG_HIDDEN` 掩码 |
| R2 `0x0803F1C1` | `_lv_ll_get_prev()` 的 Thumb 函数指针，与向上遍历一致 |
| CFSR `0x00008200` | `PRECISERR + BFARVALID`，同步精确数据总线错误 |
| HFSR `0x40000000` | 可配置 fault 升级为 HardFault |
| EXC_RETURN `0xFFFFFFFD` | Thread 模式使用 PSP，基本异常帧 |

精确访存地址：`0x2D000047 + 0x1C = 0x2D000063 = BFAR`。

这里 R0 是循环中的 `parent`，未必是最初的按钮地址。R3/R12 属于调用暂存寄存器，其固定值本身不能指明损坏原因。CFSR 未置 MMARVALID，不使用 MMFAR 作为独立证据。

## DUMP 中的直接证据

- 当前 TCB 与 `lvglThreadId` 都是 `0x20003720`，任务名 `LVGL`。
- 8 KB 栈起点 `0x1000B548`，PSP `0x1000D450` 在范围内；栈底连续 6,640 字节仍为 `0xA5`。现场不支持任务栈耗尽的推断。
- 默认组 `0x680C9720`，按键设备已恢复到默认组；焦点节点 `0x680C8B60`，对象 `0x680C7C60`，对应 Settings 返回按钮。
- 永久弹窗组 `0x680C8F40` 已经为空，`s_dialog` 44 字节全部为零，说明弹窗已经关闭。
- 全局组链表恰有两个组，链接正确。默认组有 15 个节点，前 13 个是正常 Settings 控件，最后两个是失效对象引用；所有前驱、后继、头尾链接仍匹配。

| 默认组节点 | 残留对象地址 | 分配表及实际用途 |
| --- | --- | --- |
| `0x680C3AE0` | `0x680C4060` | 分配表为 `0xFFFF`，属于 `0x680C4020` 起的 128 字节分配块内部；该块现在是返回按钮 `0x680C7C60` 的 `styles` 数组 |
| `0x680C5000` | `0x680C5160` | 分配表为 `0xFFFF`，属于 `0x680C5120` 起的 96 字节分配块内部；该块现在是 Yes / No Test 行 `0x680C2EA0` 的 `styles` 数组 |

两个地址的 `class_p` 都不再指向有效 LVGL 类。它们已被重新分配给样式数据，不能再按 `lv_obj_t` 解释。这也解释了多次出现不同 R0：释放后的内存复用内容随绘制、焦点状态和分配顺序变化。

## 精确还原这次错误读取

默认组启用 wrap。在返回按钮继续按上，遍历从链表头回绕到尾部：

1. 先检查尾部残留对象 `0x680C5160`。其被覆盖后的 state 为 `0x4EC0`，碰巧包含 `LV_STATE_DISABLED`，被跳过。
2. 再检查 `0x680C4060`。覆盖后的 state 未置 disabled，flags 为零，继续读取 parent；偏移 `+4` 已成为样式 selector，值为 `4`。
3. 将 `0x00000004` 当对象，检查 `+0x1C`，读取地址 `0x20`，值为零；随后读取 parent 所在的地址 `0x8`。
4. 启动 Flash 别名的 `0x8` 对应 BootLoader NMI 向量，提供的 BootLoader AXF 中为 `0x08005EDD`。
5. 再把 `0x08005EDD` 当对象，隐藏检查通过；其 `+4` 地址 `0x08005EE1` 的四个指令字节是 `47 00 00 2D`，被误解释为 parent `0x2D000047`。
6. 下一轮 `lv_obj_has_flag()` 执行 `ldr r0, [r0, #28]`，访问 `0x2D000063`，精确匹配 DUMP 的 BFAR。

```text
默认组残留引用 -> 0x680C4060 -> 0x00000004 -> 0x08005EDD -> 0x2D000047
最终访问地址                                               + 0x1C
                                                        = 0x2D000063
```

这条链使用提供的 BootLoader AXF 及低地址 Flash 别名重建，未执行这些地址上的代码。虽然 DUMP 不含 Flash，所提供 AXF 的字节能精确重建记录的最终 R0/BFAR。

## 源码因果链与建议修复

1. `GUI/GUI_Theme.c:452`：`GUI_M3_CreateButton()` 使用 `lv_btn_create()`。
2. `Middlewares/Third_Party/LVGL/GUI/lvgl/src/widgets/lv_btn.c:36`：按钮类设置 `group_def = TRUE`。
3. `Middlewares/Third_Party/LVGL/GUI/lvgl/src/core/lv_obj_class.c:117`：初始化时自动加入默认组。
4. `GUI/GUI_Message.c:662`：按钮直接加入弹窗组。
5. `Middlewares/Third_Party/LVGL/GUI/lvgl/src/core/lv_group.c:125`：处理旧组时只调整焦点；之后覆盖 `group_p`，没有移除旧节点。当前 AXF 反汇编也确认该路径没有调用移除函数。
6. `GUI/GUI_Message.c:737`：关闭时仅从 `lv_obj_get_group()` 返回的当前组移除按钮，无法清理默认组的残留引用；异步删除再释放按钮对象。

建议先做局部修复：在 `GUI_Message_DialogCreate()` 将每个按钮加入弹窗组前，显式从旧组移除。主按钮和次按钮都必须处理，例如对每个 `btn`：

```c
if (lv_obj_get_group(btn) != group)
{
    lv_group_remove_obj(btn);
    lv_group_add_obj(group, btn);
}
```

应继续保留正常路径的异步销毁。仅做 R0 范围过滤、增加栈大小、关闭 wrap 或保持永久弹窗组，不能修复旧组中的残留引用。另一个独立风险是异步投递失败时的同步 `lv_obj_del(scrim)` 回退，它可能重新引入事件分发期间删除对象的问题；本次没有分配失败证据，不把它当作此次根因。

## 离线复核与设备验证

工程根目录运行：

```powershell
python Tools/analyze_focus_hardfault.py
```

依赖 Python 3 和 PATH 中的 GNU Arm `arm-none-eabi-nm`、`arm-none-eabi-objcopy`。脚本校验 AXF 哈希、DUMP 校验和、异常帧、两级组链表、分配归属、栈填充，并重放反向遍历和 parent 链。本次执行成功，输出 `Reproduced: read 0x2D000063 == BFAR; parent == R0`。脚本固定针对已审计 AXF 的 32 位结构布局，重编译后会拒绝使用不同版本，避免地址误配。

修复后设备回归：以进入 Settings 后的默认组成员数 N 为基准（本 DUMP 正常成员数为 13），Confirm 和 Yes/No 分别循环至少 1,000 次，覆盖确认、取消及方向切换。打开弹窗时默认组应始终为 N、弹窗组为 1 或 2；关闭并完成异步销毁后应为 N 和 0。每轮连续向上经过返回按钮并回绕，确认不产生额外节点、悬空对象或 HardFault。

此次未重编译、烧录或执行硬件压力测试；以上为 AXF、源码及 DUMP 相互印证的离线结论。
