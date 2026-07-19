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
	u32 jpeg_eoi_injected;
	u32 fatfs_read_errors;
	u32 lcd_dma_timeouts;
	u32 max_jpeg_decode_ticks;
	u32 task_stack_space;
	u32 memory_pool_size;
	u32 memory_current_bytes;
	u32 memory_peak_bytes;
	u32 memory_largest_free_block;
	u32 memory_allocation_failures;
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
