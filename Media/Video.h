#ifndef MEDIA_VIDEO_H
#define MEDIA_VIDEO_H

#include "AVI_Parser.h"
#include "sys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_PATH_SIZE  256U

typedef enum Video_State
{
	VIDEO_STATE_IDLE = 0,
	VIDEO_STATE_OPENING,
	VIDEO_STATE_PREBUFFER,
	VIDEO_STATE_PLAYING,
	VIDEO_STATE_PAUSED,
	VIDEO_STATE_STOPPING,
	VIDEO_STATE_FINISHED,
	VIDEO_STATE_ERROR
} Video_State_t;

typedef enum Video_Result
{
	VIDEO_RESULT_OK = 0,
	VIDEO_RESULT_USER_STOP,
	VIDEO_RESULT_EOF,
	VIDEO_RESULT_OPEN_FAILED,
	VIDEO_RESULT_UNSUPPORTED,
	VIDEO_RESULT_UNSUPPORTED_AUDIO,
	VIDEO_RESULT_AUDIO_FAILED,
	VIDEO_RESULT_BAD_CONTAINER,
	VIDEO_RESULT_DECODE_FAILED,
	VIDEO_RESULT_IO_FAILED,
	VIDEO_RESULT_NO_MEMORY,
	VIDEO_RESULT_DISPLAY_BUSY,
	VIDEO_RESULT_DISPLAY_FAILED,
	VIDEO_RESULT_DISPLAY_RESUME_FAILED,
	VIDEO_RESULT_THREAD_FAILED
} Video_Result_t;

typedef struct Video_Stats
{
	u32 video_chunks;
	u32 audio_chunks;
	u32 video_frames_decoded;
	u32 video_frames_displayed;
	u32 video_frames_dropped;
	u32 jpeg_errors;
	u32 pcm_input_bytes;
	u32 pcm_output_frames;
	u32 audio_fifo_bytes;
	u32 audio_underruns;
	u32 audio_fifo_peak_bytes;
	u32 audio_dma_events;
	u32 audio_dma_flag_collisions;
	u32 audio_refill_events;
	u32 audio_refill_missed;
	u32 audio_refill_queue_peak;
	u32 audio_media_frames;
	u32 audio_replayed_frames;
	u32 audio_silence_frames;
	u32 audio_dma_half_events;
	u32 audio_dma_full_events;
	u32 audio_dma_errors;
	u32 audio_i2s_errors;
	u32 audio_i2s_underruns;
	u32 audio_late_writes;
	u32 audio_last_error;
	u32 audio_last_i2s_status;
	u32 jpeg_eoi_injected;
	u32 fatfs_read_errors;
	u32 fatfs_read_calls;
	u32 fatfs_read_bytes;
	u32 lcd_dma_timeouts;
	u32 lcd_wait_ticks;
	u32 max_lcd_wait_ticks;
	u32 max_jpeg_decode_ticks;
	u32 jpeg_arena_peak_bytes;
	u32 jpeg_arena_allocation_failures;
	u32 task_stack_space;
	u32 memory_pool_size;
	u32 memory_current_bytes;
	u32 memory_peak_bytes;
	u32 memory_largest_free_block;
	u32 memory_allocation_failures;
	u32 pending_video_frames;
	u32 pending_audio_chunks;
	u32 audio_prebuffer_bytes;
	u32 audio_deadline_frames;
	u32 avi_skipped_index_chunks;
	u32 avi_oversized_chunks;
	u32 avi_header_retry_count;
	u32 avi_resync_count;
	u32 avi_resync_from;
	u32 avi_resync_to;
	u32 avi_error_offset;
	u32 avi_error_fourcc;
	u32 avi_error_chunk_size;
	u32 pcm_tail_bytes_dropped;
	s32 avi_error_result;
	s32 pcm_last_result;
} Video_Stats_t;

int Video_Init(void);
int Video_Play(const char *path);
int Video_Pause(void);
int Video_Resume(void);
int Video_TogglePause(void);
int Video_Stop(void);
void Video_Cleanup(void);
u8 Video_IsBusy(void);
Video_State_t Video_GetState(void);
Video_Result_t Video_GetResult(void);
int Video_GetInfo(AVI_Info_t *info);
int Video_GetStats(Video_Stats_t *stats);
const char *Video_ResultText(Video_Result_t result);

#ifdef __cplusplus
}
#endif

#endif
