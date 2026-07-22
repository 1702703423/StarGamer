# AVI 视频快速切换撕裂问题结论

日期：2026-07-22

## 1. 现象

在完成以下 AVI 播放优化后：

1. 建立独立 `VideoAudioFeedTask`
2. 提高 PCM 水位
3. 改为顺序 demux + 独立音频 feeder

音频播放已经恢复稳定，但画面在快速切换、强运动或大面积亮暗变化时，会出现自上而下的撕裂感。临时将 `VIDEO_MJPEG_STRIPE_LINES` 从 8 调整为 16 后，撕裂现象明显改善。

## 2. 当前判断

该问题的主因不是音频，也不是 LCD DMA 超时，而是 SPI LCD 的显示提交方式与 ST7789 内部扫描时序没有同步。

当前 MJPEG 解码链路是：

```text
JPEG 扫描行解码
  -> RGB888 转 RGB565
  -> 累积到 8/16 行条带
  -> 立即通过 SPI DMA 写入 ST7789 GRAM
  -> 继续解码下一条带
```

也就是说，新帧不是等整帧解码完成后一次性提交，而是在一次 JPEG 解码过程中从上到下逐条带写入 LCD。ST7789 又在独立地刷新面板，当内部扫描位置撞上正在被 SPI 更新的 GRAM 区域时，就会在视觉上表现为旧帧和新帧之间的横向断层。画面变化越快、两帧差异越大，撕裂越明显。

## 3. 日志证据

三次 `videostat` 采样显示：

- `lcd_dma_timeout=0`：LCD DMA 没有超时。
- `lcd_wait_max=1`：单次等待 LCD DMA 的时间很短，SPI/LCD 不是卡死点。
- `decoded` 与 `displayed` 基本一致，`dropped` 较低：视频帧调度没有大面积失控。
- `audio dma/refill` 正常，`collide=0`、`late=0`、`dma_err=0`、`i2s_err=0`：音频 feeder 已经正常工作。
- `jpeg_max=68 ms`，而当前视频为 12 fps，单帧周期约 `83.333 ms`：最坏帧已经接近帧周期，整帧从上到下更新持续时间较长，容易暴露撕裂。

因此，问题更像“显示提交相位不稳定”，而不是“播放器吞吐不足导致卡顿”。

## 4. 为什么 16 行条带会改善

`VIDEO_MJPEG_STRIPE_LINES=16` 会带来两个效果：

1. 每帧 DMA 提交次数从约 30 次降低到约 15 次，条带边界减少。
2. 每次写入的连续区域变高，视觉上不容易看到密集的水平断层。

但这仍然不是根治。16 行条带只是让“从上到下更新”的颗粒更粗、更少；只要仍然边解码边写 LCD，且没有 TE/VBlank 同步，快速切换时仍可能看到新旧帧边界。

内存代价：

```text
8 行双条带：  320 * 8  * 2 * 2 = 10240 B
16 行双条带： 320 * 16 * 2 * 2 = 20480 B
额外占用：    10240 B
```

临时保留 16 行前，需要继续观察：

- `vmem largest`
- `vmem fail`
- `jpeg_arena_peak`
- `task_stack_free`
- `lcd_dma_timeout`
- `dropped`

如果 `vmem largest` 长期过低，或不同素材下出现 `vmem fail`，应回退到 8/12 行或释放其他视频池占用。

## 5. 推荐解决路径

### P0：保留 16 行条带继续验证

短期可以继续使用 `VIDEO_MJPEG_STRIPE_LINES=16`，并用多种素材长测确认内存余量。该方案风险低，已经有明显体感收益。

验收重点：

- 撕裂明显低于 8 行条带。
- `lcd_dma_timeout` 保持 0。
- `vmem fail` 保持 0。
- `dropped` 不明显增加。
- 音频 `late/collide/underrun/udr` 继续为 0 或稳定在可解释范围。

### P1：单帧 SRAMEX 缓冲验证

下一步建议做一个小范围 POC：

```text
MJPEG 完整解码到 SRAMEX RGB565 frame buffer
  -> 解码完成
  -> 整帧通过 LCD_Stream/LCD_Flush 提交
```

单帧 RGB565 需要：

```text
320 * 240 * 2 = 153600 B
```

当前外部 SRAM 使用率仍有余量，因此单帧缓冲应优先放在 `SRAMEX`，不要占用 80 KB 片上视频池。这个 POC 的目标不是马上提高帧率，而是验证“整帧提交”能否进一步压低撕裂。

如果单帧缓冲明显改善撕裂，但导致 `jpeg_max` 或 `dropped` 上升，再考虑双缓冲或显示 feeder。

### P2：正式双缓冲 + 显示 feeder

长期更合理的结构是：

```text
VideoTask:
  解码下一帧到 back buffer
  解码完成后提交给显示队列

VideoDisplayFeedTask:
  等待 LCD 空闲
  可选等待 TE/VBlank
  将 ready frame 连续写入 ST7789
```

优势：

- 避免边解码边暴露半帧。
- LCD DMA 传输可以和下一帧 JPEG 解码重叠。
- 后续可在显示 feeder 中集中加入 TE 同步、丢弃过期帧和统计。

代价：

```text
双 RGB565 frame buffer = 153600 * 2 = 307200 B
```

该方案应使用 `SRAMEX`，并且要避免和 LVGL 全屏缓冲、PCM FIFO、I2S DMA 缓冲争抢导致外部 SRAM 碎片化。

### P3：硬件 TE 同步

如果当前 ST7789 模块的 TE 引脚有接出，最终应考虑：

1. 发送 ST7789 `TEON` 命令启用 tearing effect 输出。
2. 将 TE 引脚接到 STM32 GPIO EXTI。
3. 在显示 feeder 中等待 TE 边沿后启动整帧或大块传输。

这是最接近真正 VBlank 同步的方案。若硬件没有接出 TE，仅靠软件无法保证完全无撕裂，只能通过整帧提交、减少条带数、降低帧率或降低画面变化幅度来减轻。

## 6. 当前建议

当前临时将 `VIDEO_MJPEG_STRIPE_LINES=16` 是正确的短期方向，已经验证有效。建议先保留该配置，并补一次修改后的 `videostat`：

```text
videostat r
播放同一段快速切换素材 1 到 3 分钟
videostat
```

重点比较：

- 8 行与 16 行下的 `lcd_wait`
- `lcd_wait_max`
- `lcd_dma_timeout`
- `jpeg_max`
- `dropped`
- `vmem largest`
- `vmem fail`

如果 16 行长测稳定，则可作为当前版本默认值；如果仍有明显撕裂，再进入单帧 SRAMEX 缓冲 POC。
