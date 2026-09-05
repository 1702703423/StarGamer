# Arduboy 移植方案

## 1. 当前内容与目标范围

`Middlewares/Third_Party/Arduboy` 当前包含：

- `src/`：一份早期 C 语言 Arduboy 兼容层。
- `Buttons/`：六按键和基本文字绘制测试。
- `Dino/`：恐龙跑酷游戏。
- `FlappyBall/`：飞行躲避游戏及其位图资源。

这些内容不是 AVR 模拟器，也不是完整的 Arduboy2 库。三个项目均通过全局 `arduboy` 函数表调用公共兼容层，并提供独立的 `*_init()` 和 `*_loop()` 入口，没有自己的 `main()`。

本次移植应定位为：**将游戏源码用 ARMCLANG 重新编译并原生运行在 STM32F407 上**。

官方 `.arduboy` 包通常包含面向 ATmega32u4 的 AVR `.hex`，不能由当前兼容层或本工程的 ARM ELF Loader 直接执行。若未来要求直接运行官方 `.arduboy/.hex`，需要单独实现 AVR CPU、定时器、EEPROM、OLED、按键和音频模拟器，不属于本方案范围。

## 2. 公共兼容层现状

当前 `src/` 已实现以下基础能力：

- 128 x 64、1 bpp framebuffer，大小为 1024 字节。
- 像素、直线、矩形、圆、三角形和位图绘制。
- 5 x 7 字体、光标和文本输出。
- 按键状态查询接口。
- 基本帧率控制接口。

直接加入 Keil 工程前必须处理以下问题：

1. `arduboy_core.h` 引用了本工程不存在的 `rng.h`。
2. 按键仍使用 `KEY_LEFT_VAL`、`KEY_ENTER_VAL` 等旧项目宏，与当前 `KEY_GetState()` BSP 不兼容。
3. `paintScreen()` 只是执行 `printf("arduboy paint\n")`，没有连接 LCD。
4. `delayHandler` 默认没有赋值，执行 `delay()` 会跳转到空函数指针。
5. `write()` 声明返回 `size_t`，但没有返回值，且没有加入 `arduboy` 函数表。
6. 定义的是 `SAFE_MODE`，绘图代码检查的却是 `PIXEL_SAFE_MODE`，负坐标可能导致 framebuffer 越界。
7. `random(a, b)` 当前包含上限 `b`，与 Arduino 的 `[a, b)` 语义不一致。
8. 每次随机调用都重新 `srand()`，应直接使用 STM32 RNG，并处理范围偏差和异常返回。
9. 帧计时使用有符号 `long` 比较，不能正确处理毫秒计数回绕。
10. 函数声明大量使用空参数列表 `func()`，应明确改为 `func(void)`。
11. 新增和适配代码应使用本工程的 `u8/u16/u32`、`s8/s16/s32` 类型。
12. 当前公共层及三个项目没有完整的来源、版本和许可证文件，发布固件前必须补齐。

公共兼容层应与 HAL、LVGL、按键 BSP 和 FreeRTOS 解耦，通过平台回调访问显示、按键、时间、随机数、音频和存档服务。

## 3. 现有项目评估

### 3.1 Buttons

- 入口：`arduboy_btn_init()` / `arduboy_btn_loop()`。
- 帧率：30 FPS。
- 功能：用方向键和 A/B 键移动 `Press Buttons!` 字符串。
- 价值：适合验证 framebuffer、字体、六按键、帧率和 LCD 刷新链路。
- 已知问题：两处调用了本工程未定义的 `Debug()`；坐标是全局变量，重新启动时需要显式复位。

Buttons 应作为第一个可运行目标，而不是作为正式游戏显示在最终列表中。

### 3.2 Dino

- 入口：`dino_init()` / `dino_loop()`。
- 目标帧率：60 FPS。
- 功能：启动动画、随机障碍、跳跃、碰撞和计分。
- 音效代码已经注释，可先无声运行。
- `intro()` 和每一帧都使用阻塞 `delay()`，必须运行在独立游戏任务中。
- `setFrameRate(60)` 当前实际没有控制主循环，游戏依赖固定的 `delay(16)`。
- 多个内部状态和资源符号没有 `static` 或游戏前缀，注册多个游戏前应收敛符号作用域。

Dino 适合作为第一个正式游戏，用来验证阻塞延时、随机数、位图裁剪和稳定帧率。

### 3.3 FlappyBall

- 入口：`flappyball_init()` / `flappyball_loop()`。
- 帧率：30 FPS。
- 功能：启动画面、固定点物理、管道生成、碰撞、计分和内存内最高分。
- 音效开关目前没有实际音频输出。
- 初始化、按键消抖、游戏结束和等待下一帧包含多个忙等待循环。
- 等待循环必须加入 `osDelay()` 和运行器停止检查，禁止长期占满 CPU。
- 大量普通 `char` 被用于负坐标和速度。ARMCLANG Cortex-M4 默认 `char` 为无符号，当前 `< 0` 判断会失效，必须改为明确的 `s8` 或 `s16`。
- 管道横坐标会从 128 移动到负数，应使用至少 `s16`，避免 8 位溢出和实现相关行为。
- 每次启动需要完整复位 `gameState`、分数、球状态和管道数组。

FlappyBall 应在 Buttons 和 Dino 稳定后移植。

## 4. 推荐模块划分

| 层级 | 建议位置 | 职责 |
|---|---|---|
| Arduboy Core | `Middlewares/Third_Party/Arduboy/src/` | framebuffer、绘图、文本和帧率逻辑，不包含 HAL/LVGL/RTOS |
| 平台适配 | `APP/Arduboy/Arduboy_Port.c/h` | LCD、按键、时间、RNG、音频和存档适配 |
| 游戏运行器 | `APP/Arduboy/Arduboy_Runtime.c/h` | 静态任务、游戏注册、启动、停止和资源清理 |
| 游戏注册表 | `APP/Arduboy/Arduboy_Games.c/h` | 游戏名称、`init`、`loop` 和可选清理入口 |
| GUI 页面 | `GUI/GUI_Arduboy.c/h` | 游戏列表、启动状态、错误状态和返回导航 |
| 显示仲裁 | `System/` | LVGL、NES 和 Arduboy 对 LCD/SPI1 的所有权切换 |

三个项目都已经提供 `init/loop` 入口，适合用静态描述表注册，不需要为每个游戏重复实现页面和任务管理代码。

## 5. 运行模型与 LVGL 单线程原则

游戏应使用独占显示模式，不应在 LVGL 定时器回调中执行游戏逻辑：

1. 用户在 `GUI_Arduboy` 页面选择游戏。
2. LVGL 线程更新页面为启动状态。
3. 运行器创建一个 CMSIS-RTOS V2 静态任务。
4. LVGL 在完成当前 `lv_timer_handler()` 和 LCD DMA 后，主动确认暂停并释放显示所有权。
5. 游戏任务执行 `init()`，随后循环调用 `loop()`。
6. 游戏任务只访问 Arduboy 平台接口，不调用任何 `lv_*` API。
7. 按下宿主退出键后，运行器设置停止标志；游戏在循环及阻塞等待中协作退出。
8. 游戏停止音频、等待 LCD DMA 完成并释放资源。
9. LVGL 线程恢复运行，并在自身线程中重新刷新当前页面。

不应直接把强制 `osThreadSuspend(lvglThreadId)` 当作长期方案，因为线程可能在持有 SPI/DMA 资源时被暂停。显示切换应采用事件标志握手：请求暂停、LVGL 确认暂停、游戏接管、游戏释放、LVGL 恢复。

RTOS 约束：

- 仅使用 CMSIS-RTOS V2 API。
- 预分配 TCB 和控制块。
- 游戏任务栈通过 `sys_thread_stack_alloc()` 从 CCM 分配，初始建议 4 KB，并在硬件上测量余量。
- 不使用原生 FreeRTOS 任务 API。
- 不使用强制任务终止作为正常退出机制。

## 6. 显示方案与内存预算

原始 Arduboy framebuffer 为 128 x 64 单色页面布局。推荐使用 2 倍整数缩放：

- 输出尺寸：256 x 128。
- 屏幕位置：`x = 32`、`y = 56`，居中显示在 320 x 240 LCD 上。
- 前景色：RGB565 白色。
- 背景色：RGB565 黑色。
- 源 framebuffer：1024 字节，仅由 CPU 访问。
- RGB565 转换缓冲：256 x 128 x 2 = 65536 字节。

RGB565 缓冲必须从 `SRAMEX` 分配，禁止使用 CCM，因为 SPI DMA 无法访问 CCM。游戏开始时清理屏幕背景，随后只刷新 256 x 128 游戏区域，避免每帧传输完整的 320 x 240 framebuffer。

SPI1 当前为 42 MHz，传输 64 KB 的理论时间约为 12.5 ms。首阶段以稳定 30 FPS 为验收目标，再测量转换和 DMA 总耗时是否满足 60 FPS。若 60 FPS 不稳定，可增加脏行检测或使用条带双缓冲。(修正：本工程APB2超频，SPI1频率84Mhz)

## 7. 按键、时间和退出

按键映射：

| Arduboy | 本工程 |
|---|---|
| Up | `KEY_GAME_UP` |
| Down | `KEY_GAME_DOWN` |
| Left | `KEY_GAME_LEFT` |
| Right | `KEY_GAME_RIGHT` |
| A | `KEY_GAME_A` |
| B | `KEY_GAME_B` |
| 宿主退出 | 按下 `KEY_GAME_EXIT`（KEY7，与 NES 退出键一致） |

`buttonsState()` 应读取 `KEY_GetState()` 的消抖状态，并转换成 Arduboy 的六个按键位。宿主退出键不得暴露为游戏 A/B 键。

`delay()` 应映射为可中断的 CMSIS-RTOS 延时：长延时按小片段执行，在片段之间检查停止标志。所有游戏中的等待按键循环也必须检查停止标志，否则 FlappyBall 可能无法正常退出。

帧计时使用 `u32` 毫秒计数和回绕安全的差值比较。未到下一帧时任务至少执行 `osDelay(1)`，禁止持续忙等。

## 8. 随机数、音频与存档

### 随机数

- 保持 Arduino `random(min, max)` 的 `[min, max)` 语义。
- 使用 STM32 硬件 RNG 或由平台层维护的轻量 PRNG。
- 不在每次调用时执行 `srand()`。
- 处理 `min >= max` 和 HAL RNG 失败。

### 音频

当前 Dino 和 FlappyBall 的音频代码均不可用，首阶段保持无声。后续可实现方波合成器，将 Arduboy tone 转换为 I2S 16-bit PCM，并复用 `audio_card_*` 接口。

音频层需要所有权仲裁，防止 Arduboy 与 Music Player 或 NES 同时打开 I2S。DMA 音频缓冲同样必须位于 DMA 可访问内存。

### 存档

FlappyBall 当前最高分只保存在 RAM，重新上电会丢失。AT24C02 总容量只有 256 字节且已保存系统设置，不建议模拟 Arduboy EEPROM。

后续应使用按游戏 ID 隔离的 SD 卡存档，例如：

`0:/Arduboy/save/<game-id>.sav`

存档应带版本、长度和校验值，并在游戏退出或数据变更后延迟写入，避免频繁访问存储设备。

### 备注：不必考虑持久化存储

## 9. 推荐实施顺序

1. 补齐第三方代码来源和许可证记录。
2. 清理公共 Core，使其在不连接平台实现时也能独立通过 ARMCLANG 语法检查。
3. 实现按键、时间、延时和 RNG 平台适配。
4. 实现显示所有权握手和 1 bpp 到 RGB565 的转换刷新。
5. 接入 Buttons，验证六键、文字、帧率和退出。
6. 接入 Dino，验证位图、随机数、阻塞延时和 60 FPS 能力。
7. 修复 FlappyBall 的有符号类型、忙等待和状态复位后再接入。
8. 创建 `GUI_Arduboy` 和静态游戏注册表。
9. 增加音频和 SD 卡存档。
10. 完成 ARMCLANG 全量构建和硬件回归测试。

## 10. 验收标准

- 使用ARMCLANG编译通过
- 所有新增线程均使用 CMSIS-RTOS V2 和静态内存。
- 所有 RGB565 和音频 DMA 缓冲位于 `SRAMEX` 或其他 DMA 可访问区域。
- 游戏线程中不存在 LVGL API 调用。
- framebuffer 绘图、裁剪、负坐标和 `INVERT` 有可重复测试。
- Buttons 六键映射正确，退出键不会被游戏消费。
- Dino 和 FlappyBall 能在任意状态下协作退出。
- 连续进入和退出游戏 100 次后，LVGL、LCD、按键、音频和内存均正常恢复。
- 实测并记录 30/60 FPS 下的转换时间、SPI DMA 时间、CPU 占用和任务栈余量。
- 断电重启后，启用存档的游戏可以恢复有效数据，损坏存档不会导致崩溃。

## 补充
- RNG在项目中可用，使用RNG来产生随机数
