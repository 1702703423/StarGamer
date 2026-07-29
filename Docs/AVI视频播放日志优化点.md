# AVI 视频播放真机日志优化点

本文基于一次 320x240、MJPG、12 fps、PCM 22050 Hz、16-bit stereo AVI 播放日志整理。日志显示当前链路已经能稳定播放，但还有几个值得继续优化和校准的点。

## 1. 本次日志结论

最新样本：

```text
video 320x240 frames=1118 fps=12.000 frame_us=83333
audio rate=22050 ch=2 bits=16 align=4 avg=88200
chunks video=490 audio=1757 decoded=490 displayed=490 dropped=0 jpeg_err=0
timing jpeg_max=74 lcd_wait=259 lcd_wait_max=1 lcd_dma_timeout=0
pcm in=3566488 out_frames=1133118 fifo=32768/12696 peak=32768 underrun=0
queue video=0 audio=16 deadline_frames=900375
audio dma=2142 half=1071 full=1071 collide=454 late=0
audio udr=0 dma_err=0 i2s_err=0
io read_calls=4228 read_bytes=8850936 read_err=0 eoi=0
jpeg_arena_peak=28160 arena_fail=0 task_stack_free=6360
vmem pool=81920 current=73152 peak=73152 largest=8736 fail=0
```

健康项：

- 视频解码和显示数量一致，`dropped=0`、`jpeg_err=0`，说明该素材在 12 fps 下没有明显视频吞吐问题。
- `lcd_dma_timeout=0` 且 `lcd_wait_max=1`，LCD DMA 没有出现长时间卡死，SPI/LCD 当前不是首要瓶颈。
- `underrun=0`、`audio udr=0`、`dma_err=0`、`i2s_err=0`，音频没有实际断流。
- `jpeg_arena_peak=28160`，低于当前 38 KB arena 预算，有继续收紧内存的空间。
- `task_stack_free=6360`，VideoTask 栈余量较大。
- `vmem fail=0`，视频池暂未出现分配失败。

需要继续关注：

- `jpeg_max=74` 已接近 12 fps 的 83.333 ms 帧周期。如果 RTOS tick 为 1 ms，最坏帧只剩约 9 ms 余量，15 fps 或更高质量 JPEG 会很危险。
- `fifo=32768/12696 peak=32768` 表示 32 KB PCM FIFO 经常被填满，当前不是音频缓冲不足，而是音频预读偏激进。
- `queue audio=16` 已达到 `VIDEO_PCM_AUDIO_QUEUE_SIZE`，而 `queue video=0`，说明前瞻 demux 容易被音频元数据和满 FIFO 卡住。
- `collide=454 / dma=2142`，约 21% DMA 事件发生合并处理。它还没造成 underrun，但表示 VideoTask 在 JPEG/LCD/SDIO 工作期间不能足够及时地服务每个半缓冲事件。
- `out_frames=1133118` 明显大于 `deadline_frames=900375`。如果这些计数在同一次播放内从 0 开始，说明音频主时钟或统计口径需要校准。
- `read_bytes / read_calls` 平均约 2.1 KB/次，I/O 仍偏碎，应拆分 MJPEG source 与 PCM 读取统计后再决定缓冲大小。
- `vmem largest=8736`，视频池最大连续空闲块不到 9 KB，虽然没有失败，但碎片和峰值余量已经不宽。

## 2. 优先优化项

### 2.1 先校准音频主时钟

当前 A/V 同步依赖 `played_frames`。日志中 `out_frames` 高于 `deadline_frames`，需要先确认这是统计未重置、DMA 事件计数提前，还是 I2S 实际输出速率不等于 AVI 头里的 22050 Hz。

建议：

- 播放前执行 `videostat r`，确保本次统计从 0 开始。
- 在 `Video_Stats_t` 增加 `play_start_tick`、`stats_tick`、`audio_elapsed_ticks`。
- 打印 `audio_expected_frames = audio_elapsed_ticks * sample_rate / tick_freq`。
- 打印 `audio_clock_error_frames = pcm_output_frames - audio_expected_frames`。
- 用示波器或逻辑分析仪测 I2S LRCLK，确认实际采样率是否接近 22050 Hz。
- 若 `pcm_output_frames` 只是“已提交/已回调”而非真实播出帧，应调整命名或改为按 DMA 半缓冲真正完成时间计数。

验收标准：

- 长时间播放时 `pcm_output_frames` 与 wall clock 推算值保持小误差。
- `deadline_frames` 与已显示视频帧的 PTS 保持一致，不出现持续音频超前或视频被迫快速追赶。

### 2.2 降低 DMA 事件合并率

`collide` 不等于错误，但它表示两个 DMA 半缓冲标志在一次服务中同时出现。当前 JPEG 最坏耗时 74 tick，而音频包约 24 ms 一次 DMA 事件，因此长 JPEG 帧期间可能积累多个音频事件。

建议按优先级尝试：

- 在视频主循环中优先处理 `VIDEO_PCM_FLAG_DMA_MASK`，再进行下一次 demux 或 JPEG 解码。
- 在 MJPEG 按条带输出的循环中提供轻量 hook，让播放器可在条带边界服务一次 PCM DMA 标志。
- 将 `VIDEO_PCM_PACKAGE_MS` 从 24 ms 试测 32 ms 或 40 ms，减少 DMA 中断频率；代价是音频延迟和单半缓冲 SRAMEX 占用增加。
- 若提高包长后 `late=0`、`underrun=0` 且 A/V 不抖，则保留较低中断频率。
- 若仍有高 `collide`，降低 JPEG 质量或帧率，而不是继续盲目加 FIFO。

建议阈值：

- `collide / dma < 5%` 作为长期目标。
- `late` 必须保持 0。
- `underrun` 和 `audio udr` 必须保持 0。

### 2.3 给 PCM FIFO 加高水位节流

本次日志中 FIFO 峰值和当前值都达到 32 KB。继续增大 FIFO 不会改善当前样本，反而会占用 SRAMEX。

建议：

- 保留低水位：`fifo < prebuffer_bytes` 时优先补音频。
- 新增高水位：例如 `fifo >= 24 KB` 时停止继续把 pending audio 读入 FIFO。
- 当 FIFO 高于高水位且视频队列偏低时，优先 demux 到足够的视频元数据，避免 `queue video=0`。
- 当 `pending_audio_count == VIDEO_PCM_AUDIO_QUEUE_SIZE` 且 FIFO 已满时，先服务 DMA 或渲染到期视频，避免前瞻 demux 空转。
- 连续测试多种 AVI 后，如果 FIFO 从未低于 12 KB，可把 `VIDEO_PCM_FIFO_SIZE` 从 32 KB 试降到 24 KB；不要在高码率或慢 SD 卡验证前直接降到 16 KB。

建议新增统计：

- `audio_fifo_min_bytes`
- `audio_fifo_full_count`
- `audio_queue_full_count`
- `video_queue_low_count`

### 2.4 拆分 I/O 统计

当前 `read_calls=4228`、`read_bytes=8850936`，平均每次约 2.1 KB。这个平均值混合了 MJPEG source 和 PCM 读取，无法判断到底是谁导致小块 I/O。

建议拆成：

- `mjpeg_read_calls`
- `mjpeg_read_bytes`
- `pcm_read_calls`
- `pcm_read_bytes`
- `max_mjpeg_read_ticks`
- `max_pcm_read_ticks`

优化方向：

- 若 MJPEG 平均读取远小于 source buffer，检查 LibJPEG source refill 逻辑是否过早小块读取。
- 若 PCM 小块读取占比高，结合 `VIDEO_PCM_PACKAGE_MS` 调大包长或减少 pending audio 分段次数。
- 若最大读取耗时偶发很高，优先检查 SD 卡簇连续性和 FatFs sector cache 放置，而不是先改 JPEG。

### 2.5 继续监控视频池与 CCM 占用

LibJPEG arena 已迁入 60 KB CCM 堆，片上视频池缩为 40 KB，只保留 DMA 可达对象。按当前 8 KB source、双 20 行 stripe、上下文和可选 RGB888 行缓冲估算，视频池峰值约 37 KB；必须通过 `sysstats`/`memfrag` 同时观察视频池和 CCM。

可回收点：

- VideoTask 栈已进入主 SRAM 64 KB任务栈专区；仍应依据 high-water 数据决定是否从 8 KB下调。
- `jpeg_arena_peak=28160`，当前 38 KB arena 有约 10 KB 余量；多素材测试后可试 32 KB。
- 视频池当前空间较紧，不得继续增加 stripe/source 大小；CCM arena 至少保留实测峰值之外的安全余量，不能只看 `fail=0`。

建议验收：

- 正常播放、暂停/恢复、停止、坏 JPEG、坏 AVI、SD 读取失败路径都测过后，再收紧栈和 arena。
- 收紧后 `largest_free_block` 仍大于最大一次临时分配需求。

### 2.6 LCD 不是首要瓶颈，但可微调条带

`lcd_wait=259`、`lcd_wait_max=1` 表示每次等待很短，累计等待来自频繁条带同步。当前 8 行条带是可用点。

建议：

- 试测 `VIDEO_MJPEG_STRIPE_LINES = 12` 或 `16`。
- 对比 `jpeg_max`、`lcd_wait`、`lcd_wait_max`、`vmem largest`。
- 如果 `jpeg_max` 明显下降且内存仍足够，可保留更大条带。
- 如果只是降低 `lcd_wait` 但压缩了视频池安全余量，则保持 8 行。

### 2.7 路径显示编码

日志中的路径出现乱码：

```text
path=0:/▒▒▒▒▒▒▒▒Ļ▒▒ε▒▒▒▒▒ ▒▒OneStep▒▒Live ▒▒▒▒.avi
```

这不影响播放链路，但会影响现场定位问题。

建议：

- shell 输出路径时复用 GUI 文件名的 OEM/UTF-8 转换逻辑。
- 无法转换的字节使用 `\xNN` 转义。
- 可额外打印短文件名或文件 index，方便用户复现同一素材。

## 3. 下一轮测试建议

每次播放前先执行：

```text
videostat r
```

然后分别记录以下样本：

- 当前素材完整播放到 EOF。
- 当前素材播放 5 分钟。
- 320x240、12 fps、不同 `q:v` 的 3 个 MJPEG 文件。
- 320x240、15 fps、PCM 22050 stereo。
- 320x240、12 fps、PCM 44100 stereo。
- 慢速 SD 卡或碎片化文件。

每组需要观察：

- `decoded/displayed/dropped/jpeg_err`
- `jpeg_max/lcd_wait/lcd_wait_max`
- `fifo current/min/peak/full_count`
- `dma/collide/late/underrun/udr`
- `deadline_frames/out_frames/audio_clock_error`
- `read_calls/read_bytes/max_read_ticks`
- `jpeg_arena_peak/task_stack_free/vmem largest/fail`

## 4. 推荐调参顺序

1. 校准 `out_frames`、`deadline_frames` 和实际 I2S LRCLK。
2. 给统计加 timestamp、音频 clock error、FIFO min/full、分源 I/O。
3. 试 `VIDEO_PCM_PACKAGE_MS = 32`，观察 `collide` 是否明显下降。
4. 增加 PCM FIFO 高水位节流，避免 FIFO 和 pending audio 队列长期满。
5. 试 12/16 行 LCD 条带。
6. 在多素材长测后收紧 VideoTask 栈和 JPEG arena。
7. 最后再决定是否提高到 15 fps 或提升 JPEG 质量。

当前日志最重要的判断是：播放链路已经稳定，下一步不要先加大缓冲，而是校准音频时钟、降低 DMA 事件合并、抑制过度音频预读，并把统计拆细到足以解释每一次瓶颈。
