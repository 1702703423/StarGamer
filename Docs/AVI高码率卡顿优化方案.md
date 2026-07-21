# Star Gamer AVI 高码率卡顿优化方案

本文基于以下材料：

- 真机 `videostat` 六次采样：前三次为低码率素材，后三次为高码率素材。
- 2026-07-21 ARMCLANG `-O3` 新 `videostat` 采样：低码率 `jpeg_max=56 ms`，高码率 `jpeg_max=66~75 ms`，但高码率 `collide/dma` 升至约 `18%~21%`，且持续出现 `queue video=0 audio=16`。
- 当前工程 `Media/Video.c`、`Media/Video_MJPEG.c`、`Media/Video_PCM.c`、`BSP/i2s.c`、`FATFS/Target/sd_diskio.c`。
- 参考工程：

```text
C:\Users\ZLK\Downloads\PuZhong_STM32F407_T100_I2S (2)\
  PuZhong_STM32F407_T100_I2S\
  Program_STM32F407_T100\
  pro2_mp3_avi_nes_pic
```

- 参考说明 `video.md`。

目标是解决 320x240、12 fps、MJPEG + PCM AVI 在高码率/高 JPEG 质量下出现的视频卡顿和潜在音频重复问题。本文只提出优化方案，不直接修改播放器代码。

## 1. 结论

当前卡顿不是单一的 SDIO 带宽问题。结合旧日志和 2026-07-21 ARMCLANG `-O3` 新日志后，优化方向应从“继续提高编译优化等级”调整为“先修复音频补缓冲与调度正确性，再继续降低 JPEG CPU 时间”。

1. **P0：音频 DMA 补缓冲事件会丢失。**
   - 旧日志中最坏 JPEG 帧占用 `144 ms`，期间约发生 6 次音频 DMA 事件。
   - 新 `-O3` 日志中高码率 `jpeg_max=66~75 ms`，已经低于 12 fps 帧周期，但仍跨越约 3 次音频 DMA 事件。
   - 高码率 `collide/dma` 从低码率的约 `1%~5%` 升至约 `18%~21%`，说明线程标志合并仍是当前最强证据。
   - 当前使用两个线程标志位表示两个 DMA 半缓冲，重复事件会被合并，不能累计次数。
   - `Video_PCM_Service()` 在两个标志同时存在时只补一个缓冲，另一个缓冲可能重复播放旧数据。
   - `collide` 只是可见的合并次数，同一个标志位被重复设置的事件不会被完整统计，因此它是丢事件数量的下界。

2. **P1：`-O3` 已把高码率 JPEG 最坏耗时拉回帧预算附近，但仍需继续测量和降本。**
   - 12 fps 的帧周期是 `83.333 ms`。
   - 旧日志低码率 `jpeg_max=57 ms`，高码率 `jpeg_max=144 ms`，当时高码率明显超过帧周期。
   - 新 `-O3` 日志低码率 `jpeg_max=56 ms`，高码率 `jpeg_max=66~75 ms`，说明编译优化已经显著降低 JPEG 热路径耗时。
   - 因此不建议继续把“更激进的全工程编译优化”作为第一优化方案；应保留 `-O3`，再用 DWT 分段统计、RGB565 直接输出和素材约束做定点优化。
   - 对偶发超过帧周期的帧仍必须主动丢帧，不能让视频解码拖慢媒体时钟。

3. **P0：视频前瞻队列被音频队列/FIFO 策略阻塞，丢帧机制没有机会工作。**
   - 新旧高码率日志都长期出现 `queue video=0 audio=16`。
   - 当前只有在队列中已经存在多个到期视频帧时，才会丢弃旧帧。
   - 队列中没有下一帧时，即使当前帧已经严重迟到，仍会完整解码，所以高码率日志仍是 `dropped=0`。
   - 新 `-O3` 最后一条高码率采样中 `out_frames - pcm_in/4 ≈ 270591`，折合约 `12.27 s`，进一步说明播放时钟和实际媒体消耗已经明显背离。

SDIO `ClockDiv=1` 对应 48 MHz SDIO 源时钟下约 16 MHz：

```text
SDIO_CK = 48 MHz / (ClockDiv + 2)
        = 48 MHz / 3
        = 16 MHz
```

参考工程使用 `ClockDiv=0`，即约 24 MHz，并启用 4-bit 总线。但高码率素材的持续读取需求只有约 226 KB/s，远低于 4-bit SDIO 的正常能力。因此把分频从较低速度改到 `1` 效果不显著是符合日志的。当前 `-O3` 复测后，主瓶颈更明确地转向音频调度、demux 队列饥饿和迟到丢帧策略，JPEG CPU 时间保留为后续定点优化项。

## 2. 日志量化分析

### 2.1 ARMCLANG `-O3` 新日志更新（2026-07-21）

新日志在 ARMCLANG `-O3` 下采样。用户标注前两次为低码率、后三次为高码率；附件中连续追加采样也呈现同一趋势：JPEG 最坏耗时已经明显下降，但高码率播放仍被音频事件合并和队列饥饿拖住。

| 指标 | 低码率代表值 | 高码率代表值 | 判断 |
|---|---:|---:|---|
| `jpeg_max` | 56 ms | 66~75 ms | 高码率已低于 83.333 ms 帧周期，`-O3` 有明显收益 |
| `queue video/audio` | 3~4 / 11~15 | 0 / 16 | 高码率仍长期没有视频前瞻，音频队列满 |
| `collide / dma` | 12/259 到 13/479 | 125/683 到 512/2457 | 高码率合并率约 18%~21%，明显异常 |
| `out_frames - pcm_in/4` | -786 到 272 | 57404 到 270591 | I2S 时钟相对真实 PCM 输入越跑越远 |
| `dropped` | 0 | 0 | 即使高码率已失同步，视频丢帧仍未生效 |
| `late` | 0 | 0~3 | 已开始出现写入活动 DMA 半缓冲风险 |
| `jpeg_err` | 0 | 0~1 | 长时间高码率压力下开始出现 JPEG 错误 |

关键修正：

- 旧方案中“高码率 `jpeg_max=144 ms > 83.333 ms`”的判断仍是旧日志事实，但不再代表 `-O3` 当前状态。
- 新日志里高码率 `jpeg_max=66~75 ms`，说明继续单纯提高优化等级的收益空间已经变小。
- 高码率卡顿仍存在时，第一优先级应转为：音频 feeder 不丢事件、demux 不让 `queue video` 长期为 0、视频迟到时主动丢帧追媒体时间。
- JPEG RGB565 直接输出、DWT 分段计时、素材参数约束仍应保留，但属于“进一步提高可显示帧率”，不是先修正确性的第一步。

### 2.2 旧日志低码率与高码率对比

| 指标 | 低码率第三次 | 高码率第三次 | 判断 |
|---|---:|---:|---|
| 视频帧率 | 12 fps | 12 fps | 帧周期均为 83.333 ms |
| `jpeg_max` | 57 ms | 144 ms | 高码率超过帧周期 60.7 ms |
| `lcd_wait_max` | 1 ms | 1 ms | LCD 阻塞不是主因 |
| `displayed` | 178 | 183 | 高码率耗费了更长播放时间才显示相近帧数 |
| `dropped` | 0 | 0 | 高码率下自适应丢帧未生效 |
| `queue video` | 5 | 0 | 高码率没有视频前瞻 |
| `queue audio` | 16 | 16 | 音频元数据队列达到上限 |
| `fifo` | 28536 B | 32768 B | PCM FIFO 已足够大，高码率时长期满 |
| `collide / dma` | 13 / 618 | 140 / 768 | 合并率从约 2.1% 升至约 18.2% |
| `late` | 0 | 1 | 已出现写入活动 DMA 半缓冲的风险 |
| `underrun` | 0 | 0 | 当前统计没有识别“旧数据重播”为 underrun |

旧日志中高码率卡顿的直接触发条件是：

```text
jpeg_max 144 ms > frame period 83.333 ms
```

这意味着最坏帧期间至少跨过一个完整的视频 deadline，也跨过多个音频 DMA deadline。新 `-O3` 日志中该条件已缓解为 `jpeg_max=66~75 ms < 83.333 ms`，但高码率仍跨过约 3 个音频 DMA 事件周期，所以音频补缓冲事件仍不能使用不可计数的 thread bit 表示。

### 2.3 音频事件丢失证据

当前音频参数：

```text
sample rate = 22050 Hz
channels = 2
bits = 16
block align = 4 B
average bytes/s = 88200 B/s
VIDEO_PCM_PACKAGE_MS = 24 ms
```

由 `Video_PCM.c` 的计算得到：

```text
package_size   = floor(88200 * 24 / 1000), 4 字节对齐
               = 2116 B
package_frames = 2116 / 4
               = 529 sample frames
DMA event period ≈ 529 / 22050
                 ≈ 23.99 ms
```

六次日志换算结果：

| 样本 | 类型 | `jpeg_max` | `out_frames - pcm_in/4` | `collide` | `collide x 529` |
|---:|---|---:|---:|---:|---:|
| 1 | 低码率 | 57 ms | -257 | 13 | 6877 |
| 2 | 低码率 | 57 ms | 272 | 13 | 6877 |
| 3 | 低码率 | 57 ms | -1315 | 13 | 6877 |
| 4 | 高码率 | 144 ms | 48411 | 85 | 44965 |
| 5 | 高码率 | 144 ms | 63223 | 114 | 60306 |
| 6 | 高码率 | 144 ms | 77506 | 140 | 74060 |

`pcm_in/4` 是从文件读入 FIFO 的 PCM sample frame 数。正常情况下：

```text
输入帧数
  = 已实际播放的媒体帧
  + FIFO 中尚未播放的帧
  + 两个 DMA 半缓冲中尚未播放的帧
```

因此播放过程中输入帧数应大于已播放媒体帧数。高码率日志却出现 `out_frames` 大幅超过输入帧数，并且差值与 `collide x 529` 高度相关，说明：

- DMA 硬件时间继续前进；
- 回调继续执行 `played_frames += package_frames`；
- 但空闲 DMA 半缓冲没有被逐次补入新的 PCM；
- DMA 在循环播放旧缓冲数据；
- 当前统计把旧数据重播误认为正常播放，不会增加 `underrun`。

低码率日志也存在 13 次合并，只是 57 ms 最坏帧通常只覆盖 2 到 3 个 DMA 事件，影响较小。旧高码率 144 ms 会覆盖约 6 个事件，新 `-O3` 高码率 66~75 ms 仍会覆盖约 3 个事件；只要补缓冲仍依赖不可计数的两个 thread bit，同一个标志位重复设置的问题就仍然存在，实际丢失事件会明显多于 `collide`。

### 2.4 高码率增加的是 JPEG 工作量，不只是文件读取量

从第三次采样中扣除 PCM 数据：

```text
低码率视频数据估算：
(2029012 - 1312948) / 183
≈ 3913 B/视频 chunk

高码率视频数据估算：
(3429249 - 1315064) / 184
≈ 11490 B/视频 chunk
```

旧日志中高码率平均压缩帧约为低码率的 2.9 倍，`jpeg_max` 则从 57 ms 增加到 144 ms，约为 2.5 倍。两者高度相关，符合 MJPEG 高质量帧具有更多非零 DCT 系数、熵解码和颜色转换工作量的特征。

新 `-O3` 日志把高码率 `jpeg_max` 降到 66~75 ms，说明 ARMCLANG 优化等级对 JPEG 热路径确实有效。但高码率素材的数据量和复杂度仍然更高，后续应通过 DWT 分段统计判断剩余时间花在 Huffman/IDCT、颜色转换、FatFs 读取还是 LCD 提交等待上。

按名义 12 fps 估算高码率持续读取需求：

```text
视频：11490 B/frame x 12 fps ≈ 137880 B/s
音频：88200 B/s
合计：约 226080 B/s，约 221 KiB/s
```

这个吞吐量本身不足以压满 4-bit、16 MHz SDIO。SD 卡碎片和单次延迟仍需测量，但不能仅凭“高码率”把问题归因于 SDIO 总带宽。

## 3. 当前实现中的根因

### 3.1 JPEG 解码是不可抢占的长任务

`Media/Video.c` 中 `video_decode_frame()` 同步调用：

```c
Video_MJPEG_DecodeFrame(&context->mjpeg, &context->file,
                        chunk, context->info.width, context->info.height);
```

`Media/Video_MJPEG.c` 随后一次性完成：

1. `f_lseek()` 到 JPEG chunk。
2. LibJPEG 流式读取。
3. Huffman、IDCT、上采样和 RGB888 输出。
4. 逐行 RGB888 转 RGB565。
5. 每 8 行提交一次 LCD SPI DMA。
6. 等待最后一次 LCD DMA。

在函数返回前，`VideoTask` 不会调用 `video_pcm_service_flags()`。旧日志中低码率最坏阻塞 57 ms，高码率最坏阻塞 144 ms；新 `-O3` 日志中高码率最坏阻塞降到 66~75 ms，但仍可能跨过约 3 个 24 ms 音频 DMA 事件周期。

### 3.2 DMA 线程标志不是事件计数器

当前两个标志：

```c
#define VIDEO_PCM_FLAG_DMA0 (1UL << 3)
#define VIDEO_PCM_FLAG_DMA1 (1UL << 4)
```

I2S DMA ISR 根据空闲半缓冲设置对应线程标志。线程标志是电平状态，不是计数队列：

```text
DMA0 -> 设置 bit0
DMA1 -> 设置 bit1
DMA0 -> bit0 已经为 1，事件次数不增加
DMA1 -> bit1 已经为 1，事件次数不增加
```

当任务最终运行时，只能看见 `DMA0 | DMA1`，无法知道期间实际发生了 2 次、4 次还是 6 次回调。

更严重的是 `Video_PCM_Service()` 在两个 bit 同时存在时只执行一次：

```c
if(flags == VIDEO_PCM_FLAG_DMA_MASK)
{
    pcm->dma_flag_collisions++;
    target_index = audio_card_get_idle_buffer_index();
}
...
return video_pcm_fill_dma(pcm, target_index);
```

因此它不能补回所有已经错过的半缓冲。

### 3.3 当前音频主时钟无法区分正常播放和旧数据重播

DMA 回调无条件执行：

```c
pcm->played_frames += pcm->package_frames;
```

这个计数代表 I2S 硬件经过的时间，但不保证对应新的媒体数据。建议拆分为：

- `audio_clock_frames`：每个 DMA half/full 事件增加固定包帧数，表示硬件时钟。
- `audio_media_frames`：按刚播放完成缓冲区的 `dma_valid_bytes[index] / block_align` 增加。
- `audio_replayed_frames`：检测同一缓冲 generation 未更新却再次播放时增加。
- `audio_silence_frames`：EOF 或 underrun 补零产生的帧数。
- `audio_refill_missed`：音频喂数任务未能在 deadline 前更新空闲缓冲的次数。

正常稳态应满足：

```text
audio_clock_frames
≈ audio_media_frames
```

如果出现 refill miss，应显式记录并进入补静音或重新预缓冲流程，不能继续以“正常无 underrun”状态运行。

### 3.4 视频队列饥饿使丢帧逻辑失效

当前 `video_pcm_render_due()` 的策略是：

1. 取出第一个到期帧。
2. 如果队列里还有更多到期帧，丢弃旧帧。
3. 解码队列中最后一个到期帧。

这个策略要求队列中已经有后续视频帧。高码率日志却是：

```text
queue video=0 audio=16
```

音频元数据队列达到 16 项，PCM FIFO 又是满的，`video_pcm_pump()` 可能被 pending audio 阻塞，无法继续向前解析到下一视频 chunk。结果是：

- 没有后续视频帧可用于判断“当前帧已经过时”；
- 当前帧即使迟到 100 ms 以上仍会被完整解码；
- `dropped` 保持 0；
- 播放速度自然下降，表现为卡顿。

### 3.5 当前文件访问不是严格顺序流

参考工程按 AVI chunk 顺序读取，使用 48 KB 缓冲一次读入当前压缩帧，然后立即解码。当前工程为了建立音频/视频前瞻，parser、PCM 和 MJPEG 会围绕同一个 `FIL` 多次 `f_lseek()`：

- parser 跳到 chunk header；
- PCM 跳到 pending audio payload；
- MJPEG 跳到 pending video payload；
- 下一次 parser 再跳回其逻辑扫描位置。

`_USE_FASTSEEK=1` 已经编译启用，但当前视频代码没有为 `FIL.cltbl` 创建 cluster link map，因此 fast seek 实际没有生效。连续文件影响可能较小，碎片文件会放大 `f_lseek()` 延迟和 FAT 链遍历开销。

这不是现有日志能确认的第一瓶颈，但属于需要处理的次要放大因素。

## 4. 与参考工程的关键差异

### 4.1 参考工程在 DMA 回调中立即补音频

参考工程 `video_player.c` 的 `video_feeding_sound_card()` 在 I2S DMA 回调链中直接执行：

```c
kfifo_out(&audio_fifo, temp_buff, pcm_size);
audio_card_write(temp_buff, pcm_size);
```

然后才唤醒视频解码任务补充 FIFO。其优点是：

- 音频补缓冲不等待 JPEG 解码结束；
- 每次 DMA 回调都立即处理当前空闲缓冲；
- JPEG 慢主要影响视频，不会先破坏音频双缓冲状态。

不建议在 Star Gamer 中原样复制所有逻辑到 ISR，因为当前声卡还支持 8-bit、单声道和音量变换，最坏路径可能在 ISR 中执行较长循环。更合适的移植方式是建立独立高优先级 AudioFeedTask。

### 4.2 参考工程直接在 LibJPEG 合并上采样中输出 RGB565

参考工程修改了 `libmjpeg/jdmerge.c`，将 YCbCr 转换结果直接写成 16-bit RGB565。`dec_mjpeg.c` 得到的扫描行已经是 RGB565，只需复制到 LCD DMA 行缓冲。

当前工程使用标准 LibJPEG RGB888 输出，再由：

```c
video_mjpeg_convert_row()
```

对 320x240 的每个像素做一次 RGB565 转换和字节交换，每帧额外处理 76800 个像素。该转换不是旧日志 144 ms 的唯一原因，也不是当前 `-O3` 后卡顿的第一证据，但属于可以明确消除的一次完整像素遍历。

参考代码存在颜色通道顺序和 LCD 字节序的定制，不能直接覆盖当前全局 LibJPEG。应增加视频专用、可编译开关控制的 RGB565 输出路径，并用红/绿/蓝色条验证颜色和端序。

### 4.3 当前 LCD 条带路径已经优于参考工程的逐行提交

参考工程每行提交一次 LCD DMA。当前工程每 8 行提交一次，320x240 一帧约 30 次 DMA 提交，并使用双条带缓冲。

日志中：

```text
lcd_wait_max=1
lcd_dma_timeout=0
```

说明 LCD DMA 已能与 JPEG 解码有效重叠，当前不应优先改成逐行方式。

### 4.4 STM32F407 没有 DMA2D 或硬件 JPEG 解码器

`video.md` 中“STM32F407 支持 DMA2D”的建议不适用于本项目。DMA2D 出现在部分 STM32F429/F7/H7 等器件中，STM32F407ZGT6 没有 DMA2D，也没有硬件 JPEG codec。

本项目可利用的是：

- Cortex-M4 整数/DSP 指令和编译器优化；
- SDIO DMA；
- SPI LCD DMA；
- I2S DMA；
- 内部 SRAM 的低延迟工作区；
- 更高效的软件 JPEG 颜色输出和调度。

## 5. 优化方案

### 5.1 P0：建立不会丢事件的音频喂数链路

#### 推荐方案：独立 AudioFeedTask

新增一个只负责 I2S 空闲半缓冲的高优先级任务：

```text
I2S DMA ISR
  -> 投递 {idle_index, sequence} 到静态消息队列
  -> AudioFeedTask 立即被唤醒
  -> 从 PCM FIFO 取一包
  -> 写入指定 idle_index
  -> 更新该缓冲 generation 和 valid_bytes
```

约束：

- 使用 CMSIS-RTOS V2。
- 使用静态 TCB、静态消息队列内存和静态栈。
- 任务栈从 CCM 分配，但 DMA 缓冲、FIFO 和 PCM 临时缓冲继续位于 `SRAMEX`。
- 建议优先级高于 VideoTask，例如 `osPriorityHigh`；最终以系统中其他实时任务为准。
- ISR 只投递事件，不做 FatFs、JPEG、LVGL 或动态分配。
- AudioFeedTask 只做 FIFO 到 DMA 缓冲的有限搬运，不做文件读取。

建议修改声卡回调接口，使 ISR 直接传递确定的空闲缓冲 index，而不是任务醒来后再次读取可能已经变化的 `curr_buf_index`：

```c
typedef void (*audio_card_tx_callback_t)(u8 idle_index, u32 sequence);
```

消息队列深度建议 4 到 8 项。AudioFeedTask 正常情况下应在下一个约 24 ms deadline 前完成，队列深度只是诊断和短暂抖动保护，不能替代实时服务。

#### 临时低改动方案：在 JPEG 条带边界服务音频

如果暂时不增加任务，可以给 `Video_MJPEG_DecodeFrame()` 增加 service hook，在每个 8 行条带完成后调用一次：

```text
jpeg_read_scanlines
  -> RGB565 转换
  -> LCD_StreamWriteAsync
  -> video_pcm_service_hook
```

8 行条带一帧共 30 个服务点，通常能把最长不可服务区间降到数毫秒。

该方案仍依赖同一任务，鲁棒性低于独立 AudioFeedTask，但可作为快速验证：如果加入 hook 后 `collide` 和音频重复显著下降，即可确认诊断。

#### 合并事件必须按故障处理

无论采用哪种方案，都不能保留当前“两个 bit 合并后补一次”的静默行为。

检测到以下条件时应增加 `audio_refill_missed`：

- 消息队列满。
- 某半缓冲还未完成新 generation 就再次成为活动缓冲。
- `audio_card_write_to_buffer()` 检测到目标 index 已经是活动缓冲。
- sequence 跳变大于 1。

初版恢复策略可以是：

1. 立即给真正空闲的缓冲写静音。
2. 记录 replay/silence/missed 帧数。
3. 连续 miss 超过阈值时暂停 I2S。
4. PCM FIFO 重新达到高水位后重启双缓冲。

不要继续重复旧 PCM 并把统计保持为 `underrun=0`。

### 5.2 P0：提高初始 PCM 水位，但不扩大 FIFO

32 KB PCM FIFO 在 88200 B/s 下可覆盖：

```text
32768 / 88200 ≈ 371 ms
```

它已经能覆盖旧日志 144 ms JPEG 帧，也能覆盖新 `-O3` 日志 66~75 ms JPEG 帧；问题是当前只按 6 个包预缓冲：

```text
12696 / 88200 ≈ 144 ms
```

这与高码率最坏 JPEG 时间几乎相等，没有 SD 读取、任务切换或其他中断余量。

建议：

- FIFO 保持 32 KB，不继续扩大。
- 初始启动水位改为 20 到 24 KB，优先试 24 KB。
- 运行低水位建议 12 到 16 KB。
- 运行高水位建议 24 到 28 KB。
- 高于高水位时停止读取 PCM payload，优先建立视频前瞻或处理到期帧。

前提是先修复音频喂数事件丢失。当前 FIFO 长期满是因为消费事件被合并，并不代表调大预缓冲参数本身可以修复问题。

### 5.3 P0：加入单帧硬迟到丢帧

当前只有“队列里存在更多到期帧”才丢帧。应增加不依赖后续队列的硬迟到判断。

在 22050 Hz、12 fps 下，每帧音频时钟跨度约为：

```text
22050 / 12 = 1837.5 sample frames
```

建议维护 JPEG 解码 EWMA 和最近最大值：

```text
decode_estimate_frames =
    decode_estimate_ticks * sample_rate / tick_frequency

late_frames =
    played_frames - frame_deadline_frames
```

满足以下任一条件时直接跳过 JPEG payload，不进入 LibJPEG：

```text
late_frames >= 0.5 到 1.0 个视频帧周期

或

late_frames + decode_estimate_frames
    > 1.0 到 1.5 个视频帧周期
```

初始可采用保守规则：

```text
hard_drop = late_frames >= frame_interval_frames
predict_drop = late_frames > 0 &&
               late_frames + decode_ewma_frames > frame_interval_frames
```

高码率素材无法逐帧达到 12 fps 时，正确行为应是：

- 音频连续；
- 视频主动丢帧并保持媒体时间；
- `dropped` 明确增加；
- 播放总时长不被视频解码拖慢。

“`dropped=0` 但整段播放变慢”不是优化目标。

### 5.4 P0：修复 `queue video=0 audio=16` 队列饥饿

#### 短期修改

在保持现有双队列结构时：

- `VIDEO_PCM_AUDIO_QUEUE_SIZE` 从 16 增加到 32 或 64。每项只有 offset 和 remaining，额外内存较小。
- 增加 `audio_queue_full_count` 和 `video_queue_empty_count`。
- 当 FIFO 高于高水位且视频队列低于 2 到 4 帧时，优先 demux chunk header，不继续填满 PCM FIFO。
- 每轮 pump 增加“字节预算/时间预算”，不能只使用 16 个 chunk 的固定预算。
- 每次进入 JPEG 前确保：
  - 音频 FIFO 不低于安全水位；
  - 至少有 1 到 2 个后续视频 metadata，或者当前帧已通过硬迟到检查。

仅增大 audio metadata 队列是缓解措施，仍可能保留随机 seek 和队列间相互阻塞。

#### 推荐结构：顺序 demux + 独立音频 feeder

更接近参考工程、也更适合 AVI 的结构是：

```text
单个 FIL 顺序游标
  -> 读取下一个 chunk header
  -> audio chunk：分段写 PCM FIFO
  -> video chunk：
       - 迟到则顺序跳过 payload
       - 到期则从当前游标流式 JPEG 解码
  -> 继续下一个 chunk

AudioFeedTask
  -> 独立从 FIFO 补 I2S DMA
```

优点：

- 文件访问恢复为近似顺序读取。
- 不需要 parser、PCM、MJPEG 在一个 `FIL` 上来回 seek。
- 不需要保存大量 pending audio metadata。
- 视频迟到时可以在遇到当前 chunk 时直接跳过。
- 32 KB FIFO 能在 JPEG 解码期间独立维持音频。

该结构要求在进入较重 JPEG 前保持足够 PCM 水位，推荐 20 到 24 KB。

### 5.5 P1：移植视频专用 RGB565 合并输出

当前每帧执行：

```text
LibJPEG YCbCr -> RGB888
RGB888 -> RGB565 + byte swap
RGB565 -> LCD DMA
```

推荐改为：

```text
LibJPEG merged upsample/color convert
  -> 直接生成 LCD 所需端序的 RGB565
  -> 条带 DMA
```

实现注意：

- 不要直接全局替换标准 LibJPEG 的 `jdmerge.c`，GUI 图片查看器也在使用该库。
- 增加视频专用编译开关或新的输出颜色空间，例如 `JCS_RGB565_VIDEO`。
- 只在 4:2:0、4:2:2 等已验证采样格式启用快速路径；其他格式回退到标准 RGB888。
- 输出端序应直接匹配 ST7789 SPI 字节顺序，取消 `color << 8 | color >> 8`。
- 用纯红、纯绿、纯蓝、灰阶、肤色图测试颜色通道和饱和度。

预期收益：

- 去掉每帧 76800 次独立 RGB888 到 RGB565 转换。
- 减少一份 960 B RGB888 行缓冲。
- 减少一次完整图像内存读取。

需要真机测量实际收益。旧日志中它未必足以单独把 144 ms 降到 83 ms 以下；新 `-O3` 日志中它更适合作为继续压低 p99/max、释放调度余量的优化。

### 5.6 P1：约束 JPEG 采样格式和素材参数

打开 JPEG header 后增加以下统计：

- JPEG chunk size。
- `num_components`。
- Y/Cb/Cr 的 `h_samp_factor` 和 `v_samp_factor`。
- progressive/baseline。
- restart interval。

设备转码预设应固定为 baseline MJPEG + YUV 4:2:0。4:4:4 会显著增加色度块数量和颜色转换负担。

建议基线命令：

```bash
ffmpeg -i input.mp4 \
  -vf "scale=320:240:force_original_aspect_ratio=decrease,pad=320:240:(ow-iw)/2:(oh-ih)/2:black,fps=12" \
  -c:v mjpeg -q:v 9 -pix_fmt yuvj420p \
  -c:a pcm_s16le -ar 22050 -ac 2 \
  -f avi output_q9.avi
```

继续准备 `q:v 7`、`9`、`11` 三档。FFmpeg 的 `q:v` 数值越小，JPEG 质量越高、压缩帧通常越大。

根据现有日志，建议初步把素材目标设为：

- 平均视频 chunk 尽量不超过 6 到 8 KB。
- `jpeg p99` 小于 70 ms。
- `jpeg max` 小于 83 ms；若不能满足，允许可控丢帧。

对复杂运动画面仍超预算时，优先选择：

1. `q:v` 从 9 增加到 11。
2. 帧率从 12 fps 降到 10 fps。
3. 降低分辨率。

不建议用高 JPEG 质量换取设备端持续超时。

### 5.7 P1：拆分 `jpeg_max`，用 DWT 周期计数定位 CPU 与 I/O

当前 `jpeg_max` 混合了：

- `f_lseek`。
- FatFs/SDIO 读取。
- JPEG header。
- Huffman/IDCT/上采样。
- RGB565 转换。
- LCD DMA 等待。

1 ms RTOS tick 对热点优化过于粗糙。建议启用 Cortex-M4 DWT `CYCCNT`，新增：

```text
max_jpeg_seek_us
max_jpeg_read_us
max_jpeg_header_us
max_jpeg_core_us
max_rgb565_convert_us
max_lcd_submit_wait_us
```

同时拆分：

```text
mjpeg_read_calls / bytes / max_us
pcm_read_calls / bytes / max_us
parser_read_calls / bytes / max_us
f_lseek_calls / max_us
```

只有测出 `f_lseek` 或 `f_read` 的 p99/max 明显占用帧预算，才继续优化 SDIO。

### 5.8 P2：如果保留随机 seek，真正启用 FatFs CLMT

当前 `_USE_FASTSEEK=1` 只是编译支持，必须给 `FIL.cltbl` 设置表并调用：

```c
file->cltbl = clmt;
clmt[0] = CLMT_ITEM_COUNT;
f_lseek(file, CREATE_LINKMAP);
```

建议：

- 从视频内存池或 SRAMEX 分配 1 到 2 KB 的 CLMT 初值。
- CLMT 只在视频打开阶段创建。
- 记录创建失败和表容量不足。
- 对连续文件、碎片文件分别测试 `max_f_lseek_us`。

如果改为严格顺序 demux，CLMT 的收益会明显降低，可不作为首要改动。

### 5.9 P2：SDIO 可测试 `ClockDiv=0`，但不要继续盲调

当前：

```text
ClockDiv = 1 -> 16 MHz
4-bit bus 在 BSP_SD_Init() 后启用
```

参考工程：

```text
ClockDiv = 0 -> 24 MHz
4-bit bus
```

可在确认 PCB 走线、上拉和 SD 卡稳定的情况下测试 `ClockDiv=0`。验收必须同时观察：

- 长时间顺序读错误。
- CRC/timeout。
- `read_err`。
- 卡状态等待超时。
- 不同品牌 SD 卡。

不要直接启用 48 MHz clock bypass。标准模式、HAL 初始化和卡能力没有充分验证前，24 MHz 是更合理的上限测试点。

即使从 16 MHz 提升到 24 MHz，也只会改善 SD 读取部分，无法消除旧日志中的 JPEG CPU 工作量，也无法修复新日志中仍然明显存在的线程标志事件丢失。

### 5.10 P2：编译器和 LibJPEG 热点配置

参考工程的 LibJPEG 使用 `-O2`。新日志已经验证 ARMCLANG `-O3` 对当前 JPEG 热路径有明显收益：高码率 `jpeg_max` 从旧日志 144 ms 降到 66~75 ms。后续建议保留 `-O3`，并查看实际 build command，确认 `Media/Video_MJPEG.c` 和 LibJPEG 热点文件没有被单文件选项降级。

可对以下模块单独测试 speed 优化：

- `jdhuff.c`
- `jidctfst.c`
- `jddctmgr.c`
- `jdmerge.c`
- `jdcolor.c`
- `Video_MJPEG.c`

若后续继续做定点性能实验，建议比较：

- 当前已验证的 ARMCLANG `-O3` 基线。
- speed 优先选项。

每种配置必须验证：

- JPEG 输出一致。
- 无 arena 越界。
- 无异常文件回归。
- Flash 占用可接受。

不要先开启全工程 LTO；本项目包含 HAL、FreeRTOS、汇编 NES 核心和多个第三方库，扩大改动范围不利于定位收益。

### 5.11 P3：LCD 和内存只做次要微调

当前 SPI1 prescaler 为 2，APB2 为 84 MHz 时 SPI 约为 42 MHz。320x240 RGB565 单帧 153600 B，理论纯传输时间约 29.3 ms，但已通过双条带 DMA 与 JPEG CPU 工作重叠。

日志证明 LCD 不是第一瓶颈：

```text
lcd_wait_max=1
lcd_dma_timeout=0
```

暂时保持 8 行条带。

当前视频池：

```text
pool=81920
current=73152
largest=8736
```

如果从 8 行改成：

- 12 行双缓冲：额外约 5120 B，余量会明显变小。
- 16 行双缓冲：额外约 10240 B，超过当前总空闲量，不可直接采用。

可以在以下回收完成后再试 12 行：

- VideoTask 栈从 8 KB 按高水位结果缩减。
- JPEG arena 从 38 KB 在多素材验证后缩减到 32 KB。
- RGB565 直接输出后回收 RGB888 行缓冲。

但由于 LCD wait 已很低，条带扩展优先级低于音频 feeder、丢帧和 JPEG RGB565 快速路径。

## 6. 推荐实施顺序

### 阶段 1：先修正确性

1. 增加 AudioFeedTask 或 JPEG 条带 service hook。
2. 将 DMA 事件从两个 thread bit 改为带 index/sequence 的消息。
3. 检测 stale buffer generation。
4. 增加 `audio_refill_missed`、`audio_replayed_frames`、`audio_media_frames`。
5. 确保 `collide` 不再被当作正常可忽略事件。

完成条件：

```text
audio_refill_missed = 0
audio_replayed_frames = 0
late = 0
audio udr = 0
```

### 阶段 2：恢复媒体时间

1. 修复 `queue video=0 audio=16`，避免音频队列/FIFO 策略长期饿死视频前瞻。
2. 增加单帧 hard-late/predicted-late 丢帧。
3. PCM 启动水位提高到 20 到 24 KB。
4. 保持 32 KB FIFO，不继续扩容。

完成条件：

- 高码率不可持续时 `dropped` 合理增加。
- 播放总时长接近文件时长。
- 音频不中断、不重复。
- 视频不会因逐帧硬解码而持续减速。

### 阶段 3：继续降低 JPEG CPU 时间

1. 增加 DWT 分段统计。
2. 实现视频专用 RGB565 merged output。
3. 保留 ARMCLANG `-O3`，确认 LibJPEG 热点文件实际使用 speed 优化。
4. 固定 baseline、YUV 4:2:0 转码格式。

完成条件：

- 低码率 `jpeg p99 < 60 ms`。
- 目标高码率档 `jpeg p99 < 75 ms`。
- 最坏超过 83 ms 时由丢帧机制吸收，不破坏音频。

### 阶段 4：再处理文件系统

1. 测量 `f_read`、`f_lseek` max/p99。
2. 保留现有结构时创建 CLMT。
3. 优先评估顺序 demux。
4. 测试 `ClockDiv=0`。
5. 对连续和碎片 AVI 分别压测。

只有当 I/O 明确占用较大帧预算时，才继续增加 source buffer 或修改 SDIO 驱动。

## 7. 建议修改点

| 文件 | 建议 |
|---|---|
| `Media/Video_PCM.c/.h` | 将 DMA 标志改为可计数事件；拆分 clock/media/replay/silence 统计；增加 feeder API |
| `BSP/i2s.c/.h` | 回调传递确定的 idle index 和 sequence；记录每个半缓冲 generation |
| `Media/Video.c/.h` | 建立 AudioFeedTask；增加 hard-late 丢帧；调整 pump 顺序和水位；扩展统计 |
| `Media/Video_MJPEG.c/.h` | 增加条带 service hook；DWT 分段计时；接入 RGB565 快速输出 |
| `Middlewares/Third_Party/LibJPEG` | 增加视频专用 RGB565 merged upsampling 编译路径，不影响 GUI 图片解码 |
| `Media/AVI_Parser.c/.h` | 为顺序 demux 提供不强制 seek 的 iterator/read API |
| `FATFS/Target` | 仅在测量证明有必要时优化；保留 DMA 对齐；评估 CLMT |
| `Debug/shell_ext.c` 或视频 shell 命令 | 扩展 `videostat` 输出和 reset |

所有新增任务必须遵守：

- CMSIS-RTOS V2。
- 静态内存分配。
- CCM 只放 CPU 栈/普通数据，不能作为 DMA 缓冲。
- DMA 缓冲和 FatFs DMA 可达对象放在内部普通 SRAM 或 `SRAMEX`。

## 8. 新增诊断字段

建议把 `videostat` 扩展为：

```text
timing:
  jpeg_last/jpeg_avg/jpeg_p99/jpeg_max
  jpeg_seek_max_us
  jpeg_read_max_us
  jpeg_core_max_us
  rgb565_max_us
  lcd_wait_max_us

audio:
  clock_frames
  media_frames
  replayed_frames
  silence_frames
  refill_events
  refill_missed
  refill_queue_peak
  max_refill_latency_us

queue:
  video current/min
  audio current/max
  video_empty_count
  audio_full_count

io:
  parser/read/seek 分项 calls/bytes/max_us

jpeg format:
  frame_bytes avg/max
  sampling=420/422/444
  progressive
```

`videostat r` 必须同时清除所有播放器、PCM 和 audio card 统计，避免不同播放会话的数据混合。

## 9. 验收测试

每个素材播放前执行：

```text
videostat r
```

测试矩阵：

```text
320x240 @ 12 fps, q:v 11, yuvj420p, PCM 22050 stereo
320x240 @ 12 fps, q:v 9,  yuvj420p, PCM 22050 stereo
320x240 @ 12 fps, q:v 7,  yuvj420p, PCM 22050 stereo
320x240 @ 10 fps, q:v 7,  yuvj420p, PCM 22050 stereo
320x240 @ 12 fps, yuvj422p
320x240 @ 12 fps, yuvj444p
连续文件和人工碎片化文件
至少三种 SD 卡
```

基础验收：

- 连续播放 30 分钟无 HardFault、死锁、内存增长。
- `audio_refill_missed=0`。
- `audio_replayed_frames=0`。
- `late=0`。
- `underrun=0`，或只在拔卡/故障注入场景出现。
- 高码率超预算时 `dropped>0`，但播放时长不被拉长。
- A/V 不出现持续漂移。
- `queue video` 不长期为 0。
- `queue audio` 不长期卡在上限。
- `jpeg_err=0`。
- `lcd_dma_timeout=0`。
- `read_err=0`。

性能验收建议：

- 可持续素材：`jpeg p99` 小于帧周期的 80%。
- 最坏 JPEG 偶发超帧周期时，丢帧后两帧内恢复到同步窗口。
- 音频 refill 最大延迟小于一个 DMA package 周期。
- SDIO 优化必须以 `max_read_us/max_seek_us` 下降为证据，不能只看 ClockDiv。

## 10. 不建议的优化方向

- **不建议继续扩大 32 KB PCM FIFO。** 当前已经足够覆盖约 371 ms，事件丢失才是根因。
- **不建议只提高 VideoTask 优先级。** 同一个任务正在执行 JPEG，优先级无法让它同时补音频。
- **不建议只把 `VIDEO_PCM_PACKAGE_MS` 调大。** 4 KB I/O 上限下包周期最多约 46 ms，仍小于旧日志 144 ms 最坏帧，也小于新 `-O3` 高码率 66~75 ms 最坏帧，只能降低事件频率，不能根治。
- **不建议优先扩大 LCD 条带。** `lcd_wait_max=1`，收益有限且视频池余量不足。
- **不建议直接复制参考工程的 48 KB 整帧缓冲。** 当前 bounded JPEG source 更节省内存，问题在调度和颜色输出。
- **不建议直接全局替换参考工程的 `jdmerge.c`。** 它包含特定 RGB/BGR 和端序假设，会影响其他 LibJPEG 使用者。
- **不建议追求 DMA2D。** STM32F407 没有 DMA2D。
- **不建议盲目启用 48 MHz SDIO bypass。** 当前没有证据表明持续带宽不足，信号完整性和卡兼容风险更高。

## 11. 最终建议

本次优化应按以下主线执行：

```text
先保证每个 I2S DMA 半缓冲都被及时、准确补充
  -> 再修复视频前瞻队列饥饿
  -> 再让视频在迟到或超预算时主动丢帧
  -> 再减少 JPEG RGB565 转换开销
  -> 最后根据分段计时决定是否优化 SDIO/CLMT
```

最关键的判断是：

```text
旧日志中高码率素材的 144 ms JPEG 最坏耗时已经超过 83.333 ms 帧周期；
新 `-O3` 日志中 JPEG 最坏耗时降到 66~75 ms，但音频事件机制仍无法承受这种长时间不可服务区间，
并且 `queue video=0 audio=16` 使视频丢帧策略没有机会工作。
```

因此，优先修复音频 feeder、视频前瞻队列和丢帧调度，能够先把“卡顿、音频重复、播放变慢”变成“音频连续、视频按能力丢帧、播放时间正确”。随后通过 RGB565 直接输出和素材约束，才有机会提高实际显示帧率。
