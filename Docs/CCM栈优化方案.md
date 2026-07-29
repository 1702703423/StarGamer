# CCM 栈优化方案

> 历史说明：本方案描述的是旧版“任务栈位于 CCM”的状态。当前实现已改为主 SRAM 64 KB任务栈专区，CCM 用于 MJPEG CPU-only 工作区；现行设计见 [内存规划重构.md](内存规划重构.md)。以下内容仅保留为生命周期回收设计的历史依据。

## 背景

当前工程把 RTOS 任务栈分配在 CCM SRAM 中。CCM 总量只有 64KB，且被多个常驻任务和临时工作线程共享。随着 GUI、NES、Arduboy、图片解码、字体安装、音乐播放等功能增加，CCM 栈空间开始吃紧。

本方案只讨论 RTOS 任务栈的生命周期优化，不调整 DMA 缓冲区。注意：CCM 仅 CPU 可访问，仍然不能作为 DMA 缓冲区使用。

## 核心结论

1. `sys_thread_stack_alloc()` 从 `SRAMCCM` 分配任务栈，底层已经提供 `sys_thread_stack_free()`，因此具备回收能力。
2. CMSIS-RTOS2 包装层在提供 `cb_mem` 和 `stack_mem` 时使用 `xTaskCreateStatic()` 创建任务。任务退出或删除时，FreeRTOS 不会释放应用传入的静态 TCB 和栈内存。
3. 临时线程不能在自己的线程函数中释放自己的栈，因为执行 `osThreadExit()` 前当前任务仍在使用这块栈。
4. 正确回收方式应由另一个上下文完成，例如 GUI poll timer、父任务或统一 cleanup 函数：确认线程已退出后，调用 `sys_thread_stack_free()`，再把 `stack_mem` 和线程句柄清空。

## 当前候选项

| 模块 | 当前栈大小 | 当前生命周期 | 可回收性 |
|------|------------|--------------|----------|
| 图片解码 `GUI_Image` | 8192B | 线程会 `osThreadExit()`，但栈常驻复用 | 高 |
| NES `GUI_NES` | 8192B | 线程会 `osThreadExit()`，但栈常驻复用 | 高 |
| Arduboy runtime | 4096B | worker 常驻等待 thread flag | 高，需改生命周期 |
| 字体安装 `GUI_Font` | 4096B | 线程会 `osThreadExit()`，但栈常驻复用 | 中 |
| WAV 播放 `Player` | 8192B | 播放线程退出后栈常驻复用 | 中 |

重点三项图片解码、NES、Arduboy 名义可回收约 20KB CCM。若连同字体安装和 WAV 播放一起整理，名义可回收约 32KB。

## Arduboy 重点分析

`APP/Arduboy/Arduboy_Runtime.c` 当前采用常驻 worker 模型：

1. `Arduboy_Runtime_Start()` 首次调用时执行 `Arduboy_Runtime_EnsureWorker()`。
2. `Arduboy_Runtime_EnsureWorker()` 分配 4096B CCM 栈并创建 `Arduboy_Runtime_Task()`。
3. `Arduboy_Runtime_Task()` 外层是 `for (;;)`，每次通过 `osThreadFlagsWait()` 等待启动请求。
4. 游戏退出后调用 `Arduboy_Runtime_CompleteExit()`，状态变为 `FINISHED` 或 `ERROR`，但线程不会退出，只是回到等待 flag 状态。
5. 因此 Arduboy 一旦启动过，就永久占用 4KB CCM 栈。

Arduboy 的平台层资源释放相对完整：`Arduboy_Port_End()` 会释放 SRAMEX 中的 RGB 输出缓冲，并清空平台回调。真正没有释放的是 CCM 任务栈。

现有 Arduboy 游戏循环基本满足协作退出要求：

1. `dino_loop()`、`flappyball_loop()`、`arduboy_btn_loop()` 都检查 `ArduboyCore_ShouldStop()`。
2. `delay()` 映射到 `Arduboy_Port_Delay()`，内部按 10ms 切片检查 stop 标志。
3. 因此将 Arduboy 改成短生命周期线程是可行的。后续新增 Arduboy 游戏时，应禁止在 `loop()` 内写不可退出的死循环。

## 推荐改造方案

### 方案 A：统一临时线程模型

建议将图片解码、NES、Arduboy、字体安装、WAV 播放统一成下面的生命周期：

```text
allocate stack
osThreadNew
worker running
worker marks finished
osThreadExit
manager observes terminated
sys_thread_stack_free
stack_mem = NULL
thread_id = NULL
```

关键规则：

1. worker 线程只负责业务清理和设置完成状态，不释放自己的栈。
2. 外部 manager 负责释放栈。GUI 模块优先放在已有 poll timer 中处理。
3. 释放前先缓存 `osThreadGetStackSpace()`，用于 UI 或调试输出。释放后句柄不可再用于查询。
4. `thread_id` 不要在线程内部过早置 NULL，否则外部无法可靠判断是否已 terminated。
5. 同一静态 TCB 可以复用，但必须等旧线程真正退出并完成 cleanup 后再创建下一次线程。

### 方案 B：Arduboy 从常驻 worker 改为单次运行线程

建议把 Arduboy runtime 改为每次启动创建线程，每次退出销毁线程：

1. `Arduboy_Runtime_Start(game_index)`：
   - 校验游戏索引。
   - 若 runtime active，返回 busy。
   - 执行 `Arduboy_Port_ResetStop()`。
   - 分配 4096B CCM 栈。
   - 创建 `Arduboy_Runtime_Task()`。
   - 状态置为 `STARTING`。

2. `Arduboy_Runtime_Task(argument)`：
   - 不再使用外层 `for (;;)` 和 `osThreadFlagsWait()`。
   - 单次执行启动延迟、`lvgl_stop_ui()`、`Arduboy_Port_Begin()`、`game->init()`。
   - 在 `while (Arduboy_Port_ShouldStop() == 0U)` 中调用 `game->loop()`。
   - 退出时调用 `Arduboy_Runtime_CompleteExit()`。
   - 记录 stack high-water。
   - 设置线程完成标志。
   - 调用 `osThreadExit()`。

3. `Arduboy_Runtime_Cleanup()` 或 GUI poll timer：
   - 检查 Arduboy runtime 是否处于 `FINISHED` 或 `ERROR`。
   - 检查 `s_thread_id` 是否已经 terminated，或等待至少一个调度周期让 idle task 清理删除任务。
   - 调用 `sys_thread_stack_free(s_thread_attr.stack_mem)`。
   - `s_thread_attr.stack_mem = NULL`。
   - `s_thread_id = NULL`。

## 图片解码和 NES 优先修正点

图片解码和 NES 当前已经是短生命周期线程，但缺少栈释放：

1. `GUI_Image_DecodeThread()` 退出后，`GUI_Image_Timer()` 已经作为外部上下文处理结果，适合增加 stack cleanup。
2. `GUI_NES_RunTask()` 退出后，`GUI_NES_PollTimer()` 已经作为外部上下文处理状态，适合增加 stack cleanup。
3. 两者应避免在线程内部把句柄过早置 NULL。先让 poll timer 看到完成状态，再统一释放栈和清句柄。

这两项改动风险低，建议先做。完成后可立刻回收 16KB CCM。

## 风险点

1. 不要在线程函数中释放自己的 CCM 栈。
2. 不要释放仍可能被 FreeRTOS 引用的 TCB 或栈。自删除任务会进入 `xTasksWaitingTermination`，由 idle task 延后清理。
3. 若在 cleanup 中用 `osThreadGetState()` 判断 terminated，需注意句柄指向的 TCB 在 idle 清理后可能不再可靠。更稳妥的方式是 worker 设置完成状态，外部延后一到数个 tick 后释放应用栈，或设计明确的 cleanup 握手。
4. 如果未来 Arduboy 游戏在 `loop()` 内长时间阻塞且不调用 `ArduboyCore_ShouldStop()`，退出和栈释放会延迟。
5. WAV 播放线程与 I2S DMA 回调关联更强，回收前必须确保 `audio_card_register_tx(NULL)` 和 `audio_card_close()` 已完成。

## 建议实施顺序

1. 先处理 `GUI_Image` 和 `GUI_NES`，因为它们已经是短生命周期线程，只需补外部 cleanup。
2. 再处理 Arduboy，将常驻 worker 改为单次运行线程。
3. 随后处理 `GUI_Font` 和 `Player`。
4. 最后复查 `defaultTask`。当前 `StartDefaultTask()` 只调用 `init()` 后返回，长期建议改成显式自删除或启动器模型，并确认其 4096B 栈是否可回收。

## 验证方法

1. 启动前通过 shell `sysstats` 记录 CCM 使用率。
2. 依次进入图片、NES、Arduboy，启动功能后退出。
3. 每次退出后等待数个 tick，再记录 CCM 使用率，确认回到启动前基线。
4. 重复多轮启动和退出，确认没有 CCM 泄漏。
5. 打印或显示各临时线程的 stack high-water，确认当前栈大小仍有余量。
6. 对 Arduboy 重点测试：
   - 正常启动和退出。
   - 启动延迟期间退出。
   - 游戏 `init()` 阶段退出。
   - 游戏运行中退出。
   - LVGL stop/start 失败路径。

## 预期收益

第一阶段完成图片解码和 NES 后，预计释放 16KB CCM 峰后常驻占用。第二阶段完成 Arduboy 后，再释放 4KB CCM 常驻占用。完整整理字体安装和 WAV 播放后，总计可减少约 32KB 临时线程栈的长期占用。
