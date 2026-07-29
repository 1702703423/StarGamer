# Star Gamer AVI 高码率卡顿优化方案

最后更新：2026-07-29

本文基于当前播放器实现和两组真机 `videostat` 日志，分析 320x240 MJPEG + PCM AVI 在不同码率下的播放能力，并给出后续优化顺序。

本次对照素材由用户标注为：

- 2 Mbps：播放流畅，320x240 @ 23.976 fps。
- 10 Mbps：播放略微卡顿，320x240 @ 25 fps。

`videostat` 当前没有输出 AVI 的名义视频码率，因此本文沿用用户标注的 2 Mbps/10 Mbps。运行时只能从已解码 JPEG 字节数估算实际送入 LibJPEG 的负载；被丢弃帧的 payload 会直接跳过，不计入 `read_bytes`，不能用该字段反推完整文件码率。

## 1. 当前结论

卡顿由两个层次的问题组成，但优先级不同。

1. **P0：10 Mbps 素材的持续 JPEG 处理能力不足，是略微卡顿的主因。**
   - 25 fps 只有 40 ms 帧预算。
   - 在 SD/FatFs 尚未出现慢 I/O 的前两次 10 Mbps 采样中，迟到丢帧率已经稳定在约 17%，有效显示帧率只有约 20.7 fps。
   - 按音频媒体时钟和成功显示帧数反推，每个成功帧占用约 48.4~48.7 ms 的有效服务时间，持续超过 40 ms 预算。
   - 2 Mbps 素材的对应值约为 42.4~42.5 ms，接近其 41.708 ms 帧周期，只需要约 1%~2% 的迟到丢帧即可维持媒体时间，主观上可以保持流畅。

2. **P1：SD/FatFs 长尾延迟是偶发严重卡顿和音频补静音的放大器，不是持续卡顿的第一原因。**
   - 10 Mbps 前两次采样中 `parser/pcm/jpeg slow=0/0/0`，但持续丢帧已经存在。
   - 第三次采样出现 `jpeg_io_max=165 ms`、`pcm_io_max=49 ms` 后，区间丢帧率升至 33.9%，最大连续丢帧达到 8 帧，并新增约 223 ms 静音。
   - 2 Mbps 素材虽然也记录到 `parser/pcm/jpeg io max=34/52/47 ms` 和多次慢 I/O，但第一次采样之后没有新增 underrun 或 silence，说明当前缓冲在低负载下可以吸收这些抖动。

3. **P0：PCM 运行水位策略尚未真正生效，会放大存储长尾。**
   - 已配置 16 KB 低水位和 28 KB 高水位，但当前播放调度没有使用这两个水位。
   - 10 Mbps 第二次采样中 FIFO 只有 3296 B，按 88200 B/s 计算仅约 37 ms 音频余量。
   - 此时一次 49~165 ms I/O 长尾必然耗尽 FIFO，即使 AudioFeedTask 和 I2S DMA 本身完全正常。

4. **LCD、I2S DMA 事件链路、AVI 容器解析和内存不是当前主瓶颈。**
   - LCD 累计等待约 1.8~2.0 ms/显示帧，`lcd_wait_max=2`，`lcd_dma_timeout=0`。
   - 两组日志均为 `audio collide=0`、`audio late=0`、`refill missed=0`、`qpeak=1`、硬件 `UDR=0`。
   - `jpeg_err=0`、`read_err=0`、`retry=0`、`resync=0`。
   - JPEG arena、视频内存池和任务栈均无分配失败。

因此当前优化主线应调整为：

```text
补齐 JPEG/IO 分段统计
  -> 用 PCM 低/高水位主动保护音频
  -> 降低高码率 JPEG 的持续处理时间
  -> 再处理 SD/FatFs 长尾
  -> LCD 和内存只做低优先级微调
```

## 2. 2026-07-29 真机对照

### 2.1 累计采样对比

下表使用每组第三次累计采样，并额外保留 10 Mbps 第二次采样作为“尚未发生慢 I/O”的基线。

| 指标 | 2 Mbps 第三次 | 10 Mbps 第二次 | 10 Mbps 第三次 | 判断 |
|---|---:|---:|---:|---|
| 名义帧率 | 23.976 fps | 25 fps | 25 fps | 2 Mbps 多 1.708 ms 帧预算，但不足以解释约 10 倍的丢帧率差异 |
| 帧周期 | 41.708 ms | 40 ms | 40 ms | 10 Mbps 预算更紧 |
| `decoded/displayed` | 2407 | 1109 | 1226 | 均无 JPEG 错误 |
| `dropped` | 46 | 233 | 293 | 每组都含启动预缓冲丢帧 6 帧 |
| `late` | 40 | 227 | 287 | 运行期迟到差异明显 |
| 迟到丢帧率 | 1.63% | 16.99% | 18.97% | 10 Mbps 持续能力不足 |
| 有效显示帧率 | 23.58 fps | 20.75 fps | 20.26 fps | 高码率实际能力约 20~21 fps |
| 有效服务时间/显示帧 | 42.51 ms | 48.40 ms | 49.56 ms | 高码率持续超过 40 ms 预算 |
| `jpeg_max` | 102 ms | 69 ms | 273 ms | 单个 max 不能代表持续性能 |
| `parser_io max/slow` | 34 ms / 1 | 1 ms / 0 | 1 ms / 0 | parser 不是高码率主因 |
| `pcm_io max/slow` | 52 ms / 27 | 2 ms / 0 | 49 ms / 5 | 第三次高码率采样出现存储抖动 |
| `jpeg_io max/slow` | 47 ms / 23 | 5 ms / 0 | 165 ms / 14 | 165 ms 长尾显著放大卡顿 |
| `lcd_wait`/显示帧 | 1.76 ms | 1.91 ms | 1.89 ms | LCD 等待稳定且很小 |
| `lcd_wait_max` | 2 ms | 2 ms | 2 ms | LCD DMA 正常 |
| `refill missed/qpeak` | 0 / 1 | 0 / 1 | 0 / 1 | AudioFeedTask 正常 |
| `underrun` | 9 | 17 | 27 | 高码率存储抖动期间新增 10 次 FIFO 欠载 |
| `silence_frames` | 4539 | 3390 | 8306 | 高码率最后区间新增 4916 帧静音 |
| 最大连续迟到丢帧 | 2 | 1 | 8 | 第三次高码率采样出现约 320 ms 画面空档 |
| 统计读取速率 | 327 KiB/s | 497 KiB/s | 498 KiB/s | 平均带宽不高，问题在处理预算和长尾 |

### 2.2 指标计算口径

迟到丢帧率排除启动预缓冲帧：

```text
late_drop_rate = late / (decoded + late)
```

有效显示帧率：

```text
effective_fps = nominal_fps * decoded / (decoded + late)
```

有效服务时间表示播放器为了处理一个成功显示帧所消耗的平均媒体时间，其中包含 JPEG 解码、对应文件读取、demux/PCM 工作和 LCD 最终等待：

```text
effective_service_ms =
    (audio_deadline_frames / audio_sample_rate) * 1000 / decoded
```

它不是纯 LibJPEG CPU 平均耗时，但可以准确判断整条视频处理链是否具备持续帧率能力。

从 `read_bytes - pcm_in` 估算每个成功解码帧实际读入 LibJPEG 的数据量：

| 样本 | JPEG 字节/成功帧 | 按名义帧率折算 |
|---|---:|---:|
| 2 Mbps 第三次 | 约 10.5 KB | 约 2.01 Mbps |
| 10 Mbps 第一次 | 约 19.6 KB | 约 3.93 Mbps |
| 10 Mbps 第二次 | 约 20.4 KB | 约 4.07 Mbps |
| 10 Mbps 第三次 | 约 20.9 KB | 约 4.18 Mbps |

这个统计只覆盖真正送入 LibJPEG 的帧。迟到丢弃帧直接跳过 payload，因此不能据此否定素材的 10 Mbps 名义码率；它能证明的是，当前成功解码的高码率 JPEG 帧平均已经约为低码率的 2 倍。

### 2.3 为什么 `jpeg_max=102 ms` 的 2 Mbps 视频仍然流畅

2 Mbps 日志说明单看 `jpeg_max` 会产生错误结论：

- `jpeg_max=102 ms` 确实超过 41.708 ms 帧周期。
- 但该值在第一次采样后没有继续增长，是一个历史峰值。
- 后续仍出现多次大于等于 20 ms 的 I/O，`underrun=9` 和 `silence=4539` 却完全不再增长。
- 最终运行期迟到丢帧率只有 1.63%，最大连续丢帧只有 2。

因此支持能力必须看平均值和分布，至少需要：

```text
jpeg average / p50 / p95 / p99 / max
end-to-end service average / p95 / p99
over-budget frame count
consecutive over-budget max
```

`max` 只适合定位最坏事件，不能单独决定素材是否可持续播放。

### 2.4 高码率持续瓶颈与 SD 长尾的证据分离

10 Mbps 前两次采样具有决定性意义：

```text
parser slow = 0
pcm slow    = 0
jpeg slow   = 0
jpeg_io_max = 5 ms
late drop   = 16.99%~17.22%
```

这证明即使没有任何达到 20 ms 阈值的文件操作，高码率视频仍只能显示约 20.7 fps。持续卡顿应首先归因于 JPEG 主路径和伴随的常规读取总成本，而不是 SD 卡平均带宽。

第二到第三次高码率采样之间：

```text
video chunks       +177
displayed          +117
dropped            +60
区间丢帧率         33.9%
jpeg slow          +14
pcm slow           +5
underrun           +10
silence_frames     +4916，约 223 ms
max consecutive    1 -> 8
```

这说明 SD/FatFs 长尾会把“稳定约 20 fps”进一步恶化成肉眼明显的停顿和音频补静音，但它是第二层问题。

## 3. 当前实现状态

2026-07-21 文档中的多个 P0/P1 项已经实现，不能继续按旧架构分析。

| 项目 | 当前状态 | 真机证据/说明 |
|---|---|---|
| 独立 AudioFeedTask | 已完成 | `osPriorityHigh`，静态消息队列，深度 8 |
| DMA idle index + sequence | 已完成 | `collide=0`、`missed=0`、`qpeak=1` |
| clock/media/replay/silence 统计 | 已完成 | 两组日志均能区分实际媒体和静音帧 |
| 32 KB PCM FIFO | 已完成 | DMA 可达 SRAMEX |
| 24 KB 启动预缓冲 | 已完成 | `fifo ... /24576` |
| 16/28 KB 运行低/高水位 | 仅定义，未接入调度 | 当前最重要的调度缺口之一 |
| 单帧 hard-late 丢帧 | 已完成 | 高码率 `late` 正常增加，播放时间不再被逐帧硬解码拖慢 |
| 近似顺序 demux | 已完成 | 旧式 video/audio metadata 队列已移除 |
| `queue video/audio` | 遗留统计字段 | 当前固定为 0/0，不能再解释为队列饥饿 |
| 视频专用 RGB565 merged output | 已完成 | 4:2:0/4:2:2 快速路径；其他格式回退 RGB888 |
| `JDCT_IFAST`、关闭 fancy upsampling | 已完成 | 当前 LibJPEG 快速配置 |
| 双条带 LCD DMA | 已完成 | 当前 20 行条带，`lcd_wait_max=2` |
| 视频热数据内部 SRAM 池 | 已完成 | 40 KB专用段保留 DMA 可达对象；LibJPEG 工作区迁入 CCM |
| parser/PCM/JPEG I/O trace | 已完成 | 已有 last/max/slow，但缺少累计和分位统计 |
| READ/SEEK 分离历史峰值 | 未完成 | 当前 max 无法知道来自 READ 还是 SEEK |
| JPEG CPU 分段 DWT 统计 | 未完成 | 无法继续细分 Huffman/IDCT/merged output |
| 实际码率和完整 chunk 统计 | 未完成 | 被丢帧 payload 未计入 `read_bytes` |

当前代码路径是：

```text
单个 VideoTask 顺序解析 AVI
  -> audio chunk 分段写 PCM FIFO
  -> video chunk 等待音频时钟
       -> 已迟到一帧：跳过 payload
       -> 否则：同步进入 LibJPEG
  -> LibJPEG RGB565 条带输出到 LCD DMA

I2S DMA ISR
  -> 投递 {idle_index, sequence}
  -> 高优先级 AudioFeedTask
  -> PCM FIFO 搬运到空闲 DMA 半缓冲
```

这个架构已经解决旧版本的 DMA thread flag 合并、旧 PCM 重播、队列饥饿和媒体时间漂移问题。剩余问题是单位时间内能解码多少 JPEG，以及遇到存储长尾时是否提前保护音频 FIFO。

## 4. 当前根因

### 4.1 JPEG 主路径持续超过 25 fps 预算

当前 `max_jpeg_decode_ticks` 包围整个 `Video_MJPEG_DecodeFrame()`，其中混合了：

- 必要的 `f_lseek()`。
- 8 KB source buffer 的多次 `f_read()`。
- JPEG header 解析。
- Huffman 熵解码。
- IDCT。
- merged upsampling + RGB565 输出，或不支持采样格式时的 RGB888 回退。
- LCD 条带提交和最后一次 DMA 等待。

10 Mbps 无慢 I/O基线仍需约 48.4 ms/成功帧，而预算只有 40 ms。要做到刚好 25 fps，整条成功帧路径至少需要降低约 17% 的时间：

```text
(48.4 - 40.0) / 48.4 ~= 17.4%
```

为了给 SD 抖动、音频任务抢占和其他中断留余量，工程目标不应是刚好 40 ms，而应把常态成功帧处理时间压到约 32~35 ms。

高码率成功帧平均 JPEG 输入约为低码率的 1.9~2.0 倍。MJPEG 质量提高不仅增加读取字节，还通常增加非零 DCT 系数和 Huffman/IDCT 工作量，因此码率与 CPU 时间相关，但不呈简单线性关系。

### 4.2 SD/FatFs 存在明显长尾

当前平均读取速率约为：

```text
2 Mbps 样本：约 327 KiB/s
10 Mbps 样本：约 498 KiB/s
```

该吞吐远低于 4-bit SDIO 的持续能力。问题是个别文件操作达到 34~165 ms，而不是平均带宽耗尽。

现有日志仍无法回答：

- 165 ms 来自 `f_read` 还是 `f_lseek`。
- 时间花在 FAT cluster 遍历、SD DMA 完成等待还是 card ready 等待。
- 慢操作是否集中在碎片边界。
- 目标缓冲是否走了对齐 DMA 快路径。

因此不能只调 `ClockDiv`，也不能只创建 CLMT；必须先把长尾分层记录。

### 4.3 低/高水位没有参与运行调度

当前 `Video_PCM_Open()` 计算：

```text
prebuffer = 24 KB
low water = 16 KB
high water = 28 KB
```

但 `Video.c` 只使用了 `prebuffer_bytes`。运行阶段在遇到视频 chunk 时只按音频 deadline 判断是否迟到，没有在 FIFO 低于 16 KB 时主动跳过视频以向前解析更多音频。

这会形成以下放大链：

```text
JPEG 平均耗时超过帧预算
  -> FIFO 在某些时刻降到很低
  -> 下一次 SD/FatFs 长尾耗尽剩余 PCM
  -> AudioFeedTask 正常补零
  -> silence 和 underrun 增加
  -> 视频同时连续丢帧追赶媒体时间
```

AudioFeedTask 保证了 DMA 事件不会丢，但不能凭空产生尚未从文件读取的 PCM 数据。

### 4.4 统计仍不足以定位 JPEG 内部热点

当前可以确认“整条 JPEG 路径过慢”，但不能严谨断定主要 CPU 时间位于 Huffman、IDCT、RGB565 merged output 还是多次普通 I/O。

在没有 DWT 分段统计前，不应：

- 再次重写 RGB565 路径。
- 盲目扩大 source buffer。
- 缩小 JPEG arena。
- 调整 LCD 条带。
- 对整个工程启用更激进优化或 LTO。

## 5. 优化方案

### 5.1 P0：补齐可用于决策的统计

#### JPEG 分段 DWT 统计

使用 Cortex-M4 DWT `CYCCNT`，按帧累计并输出：

```text
jpeg_total_us       average/p50/p95/p99/max
jpeg_seek_us        total/p95/max
jpeg_read_us        total/p95/max
jpeg_header_us      average/p95/max
jpeg_scanline_us    average/p95/max
jpeg_finish_us      average/p95/max
lcd_wait_us         average/p95/max
over_budget_frames
```

`jpeg_scanline_us` 可以进一步按 LibJPEG 内部热点实验拆分，但第一版不应在每个 MCU 扫描行打印日志，只累计计数器和直方图。

建议使用固定桶，避免保存所有样本：

```text
<20 ms, 20~30, 30~35, 35~40, 40~50, 50~80, 80~120, >=120 ms
```

#### I/O 分层统计

parser、PCM、JPEG 和通用 skip 分别记录：

```text
read calls/bytes/total_us/p95/max
seek calls/total_us/p95/max
max operation/offset/request_bytes
```

在 `sd_diskio.c` 增加：

```text
aligned_fast_path_count
scratch_slow_path_count
dma_wait_max_us
card_ready_wait_max_us
retry/error_count
blocks_per_request average/max
```

#### 素材和运行环境统计

`videostat` 还应输出：

```text
CPU clock profile / SystemCoreClock
SDIO ClockDiv / 1-bit or 4-bit runtime state
video chunk count/bytes average/p95/max，包括被丢弃帧
JPEG sampling 420/422/444
RGB565 direct frames / RGB888 fallback frames
baseline/progressive/restart interval
FIFO minimum and time below low water
startup underrun/silence 与 runtime underrun/silence 分离
```

完成这些统计后，才能把 48 ms 拆成可优化的 CPU 和 I/O 部分。

### 5.2 P0：让 PCM 低/高水位真正控制调度

进入 JPEG 前增加音频保护策略：

```text
if fifo_bytes < low_water:
    进入 audio recovery 模式
    当前 video chunk 直接跳过，不进入 LibJPEG
    继续顺序 demux

while recovery:
    audio chunk -> 写 FIFO
    video chunk -> 跳过并记录 low_water_drop
    fifo_bytes >= recovery_target -> 退出 recovery
```

初始建议：

```text
low_water      = 16 KB，约 186 ms PCM
recovery_target = 24 KB，约 279 ms PCM
high_water     = 28 KB，约 325 ms PCM
```

使用低/高水位滞回，避免在阈值附近反复切换。还必须处理：

- EOF 时不能永久等待高水位。
- seek/pause 后重新预缓冲。
- 音频 chunk 稀疏或异常 AVI 的最大恢复扫描预算。
- recovery 期间仍响应 stop/seek/pause。

新增统计：

```text
low_water_enter_count
low_water_drop_count
fifo_min_bytes
fifo_below_low_ticks
recovery_max_ticks
```

该策略不会提高 JPEG 原始帧率，但能把“视频和音频一起顿”转换为“主动少显示几帧、音频连续”。

### 5.3 P0：加入预测性丢帧，避免解码注定迟到的帧

当前 hard-late 只有在音频时钟已经领先整整一个视频帧周期时才丢帧。高码率下，一帧开始时可能只迟到数毫秒，但已知平均解码还需要约 48 ms，继续解码必然让下一帧迟到。

维护 JPEG 成功帧 EWMA：

```text
decode_estimate_frames =
    jpeg_ewma_ticks * audio_rate / tick_frequency

late_frames = played_frames - deadline_frames
```

保守预测规则：

```text
hard_drop = late_frames >= frame_interval_frames

predict_drop = late_frames > 0 &&
               late_frames + decode_estimate_frames > frame_interval_frames
```

统计必须区分：

```text
prebuffer_drop
hard_late_drop
predicted_late_drop
low_water_drop
oversized_drop
```

预测性丢帧的目标是减少抖动和连续丢帧，不是掩盖 JPEG 性能不足。验收时仍需报告有效显示帧率。

### 5.4 P1：根据 DWT 结果降低 JPEG 持续成本

当前已经启用 `JDCT_IFAST`、关闭 fancy upsampling，并实现了视频专用 RGB565 merged output。下一步应根据分段结果选择改动。

#### 如果 Huffman/IDCT 占主导

- 确认 `jdhuff.c`、`jidctfst.c`、`jdcoefct.c`、`jdmerge.c` 实际使用 ARMCLANG `-O3`/speed 优化。
- 检查 map/listing，避免热点文件被单文件配置降级。
- 评估 Cortex-M4 DSP 指令友好的定点实现，但必须保持 LibJPEG 异常输入和 arena 边界行为。
- 不先开启全工程 LTO；本项目包含 HAL、FreeRTOS、汇编 NES 核心和多个第三方库，扩大范围不利于定位收益。

#### 如果普通 JPEG 读取累计耗时较高

- 统计每帧 8 KB source refill 次数和累计时间。
- 评估 12/16 KB source buffer，而不是直接复制参考工程的 48 KB 整帧缓冲。
- 当前 40 KB视频池在 8 KB source、双 20 行 stripe 和可选 RGB888 行缓冲下预计仅余约 3 KB，扩大 source buffer 前仍须重新规划内存。
- 不建议直接把 LibJPEG 热工作区整体迁移到 FSMC SRAM；CPU 随机访问外部 SRAM 可能抵消减少 I/O 调用的收益。

#### 如果 RGB888 fallback 被命中

- 记录不满足快速路径的具体采样因子。
- 素材统一转为 baseline YUV 4:2:0 或已经验证的 4:2:2。
- 不全局修改 GUI 图片查看器使用的 LibJPEG 行为。

#### 性能目标

25 fps 的工程目标：

```text
end-to-end successful-frame average <= 35 ms
jpeg p95 <= 35 ms
jpeg p99 <= 40 ms，或由预测丢帧吸收
late drop rate <= 5%
effective displayed fps >= 23.75 fps
```

2 Mbps 兼容档应保持：

```text
late drop rate <= 2%
max consecutive late drops <= 2
runtime underrun/silence 不增长
```

### 5.5 P1：建立设备端素材约束

本次 2 Mbps 流畅样本中，成功解码 JPEG 平均约 10.5 KB；10 Mbps 卡顿样本中约为 19.6~20.9 KB。可先把平均 JPEG chunk 10~12 KB 作为 24/25 fps 兼容档经验基线，再通过更多素材确定 p95 上限。

推荐准备以下统一测试档：

```text
320x240 @ 24000/1001 fps, yuvj420p, q:v 11/9/7
320x240 @ 25 fps,         yuvj420p, q:v 11/9/7
PCM s16le, 22050 Hz, stereo
```

FFmpeg 示例：

```bash
ffmpeg -i input.mp4 \
  -vf "scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2:black,fps=25" \
  -c:v mjpeg -q:v 11 -pix_fmt yuvj420p \
  -c:a pcm_s16le -ar 22050 -ac 2 \
  -f avi output_q11.avi
```

FFmpeg 的 `q:v` 越小，JPEG 质量越高、帧通常越大。素材验收不应只看容器显示的 Mbps，还要看：

```text
video chunk average/p95/max
jpeg total average/p95/p99
over-budget frame ratio
RGB565 direct-path hit rate
```

如果必须立即获得稳定播放而暂不修改解码器，优先使用接近本次 2 Mbps 样本的兼容档，或把 25 fps 降到当前实测可持续的 20 fps 左右。后者是素材侧降级，不是固件性能优化的最终目标。

### 5.6 P1：定位并削弱 SD/FatFs 长尾

建议按以下顺序验证：

1. 同一 AVI、同一固件，复制到新格式化 SD 卡后测试。
2. 同一文件测试至少两种 SD 卡。
3. 对连续文件和人工碎片文件分别记录 READ/SEEK p95/max。
4. 确认慢操作来自 `f_read` 还是 `f_lseek`。
5. 如果碎片和 seek 明显相关，再创建 FatFs CLMT。
6. 如果 DMA/card ready 等待占主导，再检查 SDIO 驱动状态机、IRQ 和卡兼容性。

当前 `_USE_FASTSEEK=1` 只表示 FatFs 编译支持。若需要 CLMT，必须设置 `FIL.cltbl` 并调用：

```c
file->cltbl = clmt;
clmt[0] = CLMT_ITEM_COUNT;
f_lseek(file, CREATE_LINKMAP);
```

当前源码为 48 MHz SDIO 源、`ClockDiv=2`，实际约 12 MHz，并在 BSP 初始化后切换为 4-bit。可以在确认 PCB 信号完整性和多卡稳定性后对照测试：

```text
ClockDiv=2 -> 12 MHz
ClockDiv=1 -> 16 MHz
ClockDiv=0 -> 24 MHz
```

提高 SDIO 时钟可能降低普通读取时间，但不能解释或修复“无慢 I/O 时仍有约 17% 丢帧”的持续 JPEG 瓶颈，也未必能消除 SD 卡内部 100 ms 级长尾。

### 5.7 P3：LCD 和内存暂不优先调整

当前 LCD 数据表明双条带 DMA 已与 JPEG CPU 工作有效重叠：

```text
lcd_wait average ~= 1.8~2.0 ms/displayed frame
lcd_wait_max = 2 ms
lcd_dma_timeout = 0
```

继续扩大 20 行条带的收益很小。当前视频池：

```text
pool    = 81920 B
current = 79872 B
largest = 2016 B
fail    = 0
```

在没有先回收其他对象前扩大条带或 source buffer会直接造成分配失败。JPEG arena 峰值约 27840 B，但缩小 38 KB arena 之前必须对多种 JPEG 采样格式和异常文件做回归。

## 6. 推荐实施顺序

### 阶段 1：测准持续时间和长尾来源

1. 增加 JPEG DWT 分段统计和耗时直方图。
2. 拆分 READ/SEEK 的 total/p95/max。
3. 增加完整 video chunk 字节统计、JPEG sampling 和 RGB565 命中率。
4. 输出 CPU/SDIO 实际时钟。
5. 分离 startup 与 runtime underrun/silence。

完成条件：能够解释一帧约 48 ms 中 CPU、普通 I/O 和 LCD 分别占多少时间，并能定位 165 ms 长尾所在层级。

### 阶段 2：优先保护音频连续性

1. 将 16/28 KB 低/高水位接入顺序 demux 调度。
2. 低水位时主动跳过视频，恢复到至少 24 KB。
3. 增加预测性丢帧，避免解码注定迟到的帧。
4. 保持现有 AudioFeedTask 和 32 KB FIFO，不重写已经正常的 DMA 事件链。

完成条件：10 Mbps 压力测试和 SD 长尾期间 `runtime silence=0`、`refill_missed=0`、`audio udr=0`。

### 阶段 3：提高高码率实际显示帧率

1. 根据 DWT 结果优化 Huffman/IDCT、source refill 或格式 fallback。
2. 确认 LibJPEG 热点实际使用 speed 优化。
3. 固定 baseline YUV 4:2:0 兼容素材格式。
4. 将成功帧常态处理时间从约 48 ms 降到 32~35 ms。

完成条件：25 fps 目标素材有效显示帧率至少 23.75 fps，迟到丢帧率不高于 5%。

### 阶段 4：处理文件系统长尾

1. 用连续文件、碎片文件和多张 SD 卡复测。
2. 根据分层数据决定是否启用 CLMT。
3. 对照测试 `ClockDiv=2/1/0`。
4. 只在证据支持时修改 source buffer 或 SDIO 驱动。

完成条件：普通 READ/SEEK p99 显著下降，100 ms 级长尾不再导致音频 FIFO 耗尽。

## 7. 验收测试

每次播放新素材前执行：

```text
videostat r
```

至少在相同源视频、相同音频、相同 SD 卡和相同 CPU profile 下测试：

```text
2 / 4 / 6 / 8 / 10 Mbps 标称档
320x240 @ 24000/1001 fps
320x240 @ 25 fps
q:v 11 / 9 / 7
yuvj420p / yuvj422p / yuvj444p
连续文件 / 人工碎片文件
至少三种 SD 卡
```

本次 2 Mbps 和 10 Mbps 样本帧率分别为 23.976 和 25 fps，不是严格单变量实验。后续必须增加相同帧率、相同画面内容、只改变 JPEG 质量/码率的配对素材，才能精确绘制码率与解码时间曲线。

### 基础正确性

- 连续播放 30 分钟无 HardFault、死锁、内存增长。
- `audio_refill_missed=0`。
- `audio_replayed_frames=0`。
- `audio udr=0`、`dma_err=0`、`i2s_err=0`。
- 启动完成后 `runtime underrun/silence` 不增长。
- `jpeg_err=0`、`read_err=0`、`retry=0`、`resync=0`。
- `lcd_dma_timeout=0`。
- 播放总时长接近文件时长，音频无重复和持续漂移。

### 2 Mbps 兼容档

- 迟到丢帧率不高于 2%。
- 有效显示帧率不低于名义帧率的 98%。
- 最大连续迟到丢帧不高于 2。
- I/O 慢事件不造成 runtime silence。

### 10 Mbps 压力档

正确性阶段允许主动丢视频，但必须满足：

- 音频连续，`runtime silence=0`。
- `refill_missed=0`。
- 播放时间不被视频解码拖慢。
- SD 长尾结束后两帧内恢复到正常调度窗口。
- 丢帧原因统计可解释，不使用 `dropped` 总数掩盖来源。

性能优化阶段目标：

- 迟到丢帧率不高于 5%。
- 有效显示帧率不低于 23.75 fps。
- JPEG/整帧 p95 和 p99 满足第 5.4 节目标。

## 8. 不建议的方向

- **不建议重新实现 AudioFeedTask。** 当前 `audio collide=0`、`refill missed=0`、`audio late=0`，事件链路已经正常。
- **不建议再按 `queue video=0 audio=0` 判断饥饿。** 这是旧队列架构遗留统计字段。
- **不建议只看 `jpeg_max`。** 2 Mbps 流畅样本已经证明历史峰值不能代表持续性能。
- **不建议只提高 VideoTask 优先级。** 同步 JPEG 的总 CPU 工作量不会因此减少，还可能挤压其他任务。
- **不建议继续扩大 32 KB PCM FIFO。** 应先使用现有低/高水位并改进调度。
- **不建议优先扩大 LCD 条带。** LCD 等待很低且视频池只剩约 2 KB。
- **不建议重复实现 RGB565 merged output。** 当前快速路径已经存在，应先统计命中率和 CPU 分段时间。
- **不建议直接复制 48 KB 整帧 JPEG 缓冲。** 当前内部 SRAM 无此余量。
- **不建议把卡顿简单归因于 SDIO 频率。** 无慢 I/O 时高码率已经持续超预算。
- **不建议盲目启用 48 MHz SDIO bypass。** 信号完整性和卡兼容风险高，当前最高只建议验证 24 MHz。
- **不建议追求 DMA2D。** STM32F407 没有 DMA2D 或硬件 JPEG 解码器。

## 9. 历史结论的状态

2026-07-21 版本文档针对的是旧播放器架构，其核心问题包括：

- 用不可计数 thread flags 表示 I2S DMA 事件。
- `queue video=0 audio=16` 的双队列饥饿。
- 没有单帧 hard-late 丢帧。
- 旧 PCM 数据重复播放无法识别。
- RGB888 再逐像素转换 RGB565。
- 8 行 LCD 条带。
- 启动 PCM 水位不足。

这些问题在当前代码中已经完成重构或替换。旧日志仍可用于解释架构演进，但不能再用于确定当前优化优先级。

当前最关键的判断更新为：

```text
2 Mbps 流畅样本即使存在偶发 34~102 ms 峰值，
仍能以约 1%~2% 迟到丢帧维持 23.976 fps 媒体时间；

10 Mbps 卡顿样本在没有任何慢 I/O 时，
成功帧有效服务时间已经约 48.4 ms > 40 ms，
因此持续 JPEG 处理能力是第一瓶颈；

后续 165 ms JPEG I/O 长尾又把区间丢帧率提高到 33.9%，
并在 FIFO 低水位条件下产生约 223 ms 新静音，
因此 SD 长尾和未启用的 PCM 水位调度是第二层放大因素。
```
