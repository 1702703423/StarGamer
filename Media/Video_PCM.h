#ifndef MEDIA_VIDEO_PCM_H
#define MEDIA_VIDEO_PCM_H

#include "AVI_Parser.h"
#include "cmsis_os2.h"
#include "kfifo.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_PCM_FIFO_SIZE       (32UL * 1024UL)
#define VIDEO_PCM_IO_BUFFER_SIZE  (4UL * 1024UL)
#define VIDEO_PCM_PREBUFFER_BYTES (24UL * 1024UL)
#define VIDEO_PCM_LOW_WATER_BYTES (16UL * 1024UL)
#define VIDEO_PCM_HIGH_WATER_BYTES (28UL * 1024UL)

typedef enum Video_PCMResult
{
	VIDEO_PCM_OK = 0,
	VIDEO_PCM_WOULD_BLOCK = 1,
	VIDEO_PCM_ERROR = -1,
	VIDEO_PCM_IO_ERROR = -2,
	VIDEO_PCM_FORMAT_ERROR = -3,
	VIDEO_PCM_MEMORY_ERROR = -4,
	VIDEO_PCM_AUDIO_ERROR = -5
} Video_PCMResult_t;

typedef struct Video_PCMStats
{
	u32 input_bytes;
	u32 output_frames;
	u32 fifo_peak_bytes;
	u32 underruns;
	u32 dma_events;
	u32 dma_flag_collisions;
	u32 refill_events;
	u32 refill_missed;
	u32 refill_queue_peak;
	u32 media_frames;
	u32 replayed_frames;
	u32 silence_frames;
	u32 dma_half_events;
	u32 dma_full_events;
	u32 dma_errors;
	u32 i2s_errors;
	u32 i2s_underruns;
	u32 late_writes;
	u32 last_audio_error;
	u32 last_i2s_status;
	u32 fatfs_read_errors;
	u32 fatfs_read_calls;
	u32 fatfs_read_bytes;
} Video_PCMStats_t;

typedef struct Video_PCM
{
	struct __kfifo fifo;
	u8 *fifo_storage;
	u8 *io_buffer;
	u8 *feed_buffer;
	osThreadId_t feed_thread;
	osMessageQueueId_t feed_queue;
	u32 sample_rate;
	u32 block_align;
	u32 package_size;
	u32 package_frames;
	u32 prebuffer_bytes;
	u32 low_water_bytes;
	u32 high_water_bytes;
	u32 dma_timeout_ticks;
	volatile u32 played_frames;
	volatile u32 media_frames;
	volatile u32 replayed_frames;
	volatile u32 silence_frames;
	volatile u32 last_dma_tick;
	volatile u32 dma_valid_bytes[2];
	volatile u32 dma_generation[2];
	volatile u32 dma_played_generation[2];
	volatile u32 dma_events;
	volatile u32 dma_flag_collisions;
	volatile u32 refill_events;
	volatile u32 refill_missed;
	volatile u32 refill_queue_peak;
	volatile u32 feed_last_sequence;
	u32 input_bytes;
	u32 fifo_peak_bytes;
	u32 underruns;
	u32 fatfs_read_errors;
	u32 fatfs_read_calls;
	u32 fatfs_read_bytes;
	volatile u8 opened;
	volatile u8 started;
	volatile u8 feed_stop;
	volatile u8 feed_exited;
	volatile u8 feed_error;
	u8 paused;
	u8 input_eof;
} Video_PCM_t;

int Video_PCM_Open(Video_PCM_t *pcm, const AVI_Info_t *info);
void Video_PCM_Close(Video_PCM_t *pcm);
int Video_PCM_ReadChunkPart(Video_PCM_t *pcm, FIL *file,
	u32 *offset, u32 *remaining);
int Video_PCM_Start(Video_PCM_t *pcm);
int Video_PCM_CheckHealth(const Video_PCM_t *pcm);
int Video_PCM_Pause(Video_PCM_t *pcm);
int Video_PCM_Resume(Video_PCM_t *pcm);
void Video_PCM_SetInputEof(Video_PCM_t *pcm);
u32 Video_PCM_GetBufferedBytes(const Video_PCM_t *pcm);
u32 Video_PCM_GetFreeBytes(const Video_PCM_t *pcm);
u32 Video_PCM_GetPlayedFrames(const Video_PCM_t *pcm);
u8 Video_PCM_IsDrained(const Video_PCM_t *pcm);
void Video_PCM_GetStats(const Video_PCM_t *pcm, Video_PCMStats_t *stats);

#ifdef __cplusplus
}
#endif

#endif
