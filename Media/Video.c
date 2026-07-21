#include "Video.h"

#include "LCD.h"
#include "Malloc.h"
#include "System_Status.h"
#include "Video_Memory.h"
#include "Video_MJPEG.h"
#include "Video_PCM.h"
#include "cmsis_os2.h"
#include "i2s.h"
#include "key.h"
#include "shell.h"
#include "shell_support.h"

#include <string.h>

extern Shell shell;

#define VIDEO_TASK_STACK_SIZE             8192U
#define VIDEO_LAUNCH_DELAY_TICKS          80U
#define VIDEO_CONTROL_POLL_TICKS          5U
#define VIDEO_CLEANUP_DELAY_TICKS         2U
#define VIDEO_KEY_RELEASE_TIMEOUT_TICKS   1000U
#define VIDEO_MAX_CONSECUTIVE_JPEG_ERRORS 8U
#define VIDEO_PCM_WAIT_TICKS              2U

#define VIDEO_FLAG_PAUSE                  (1UL << 0)
#define VIDEO_FLAG_RESUME                 (1UL << 1)
#define VIDEO_FLAG_STOP                   (1UL << 2)
#define VIDEO_FLAG_CONTROL_MASK           (VIDEO_FLAG_PAUSE | VIDEO_FLAG_RESUME | VIDEO_FLAG_STOP)
#define VIDEO_FLAG_THREAD_MASK            VIDEO_FLAG_CONTROL_MASK

#define VIDEO_CONTROL_NONE                0U
#define VIDEO_CONTROL_PAUSE               1U
#define VIDEO_CONTROL_RESUME              2U
#define VIDEO_CONTROL_STOP                3U
#define VIDEO_CONTROL_ERROR               4U

typedef struct Video_Context
{
	FIL file;
	AVI_Parser_t parser;
	Video_MJPEG_t mjpeg;
	Video_PCM_t pcm;
	AVI_Info_t info;
	Video_Stats_t stats;
	char path[VIDEO_PATH_SIZE];
	osThreadId_t thread_id;
	volatile Video_State_t state;
	volatile Video_Result_t result;
	volatile u32 exit_tick;
	u32 key_previous;
	u32 deadline_whole_ticks;
	u32 deadline_fraction_ticks;
	u32 deadline_fraction_accumulator;
	u32 audio_deadline_frames;
	u32 audio_deadline_whole_frames;
	u32 audio_deadline_fraction_frames;
	u32 audio_deadline_fraction_accumulator;
	u32 active_audio_offset;
	u32 active_audio_remaining;
	u32 active_audio_end;
	u32 consecutive_jpeg_errors;
	u32 pcm_fatfs_read_errors;
	u32 pcm_fatfs_read_calls;
	u32 pcm_fatfs_read_bytes;
	u8 file_opened;
	u8 mjpeg_opened;
	u8 pcm_opened;
	u8 demux_eof;
	u8 active_audio_pending;
	u8 lcd_owned;
} Video_Context_t;

typedef StaticTask_t osStaticThreadDef_t;

static void video_task(void *argument);
static Video_Context_t *video_get_context(void);
static __attribute__((noinline)) void video_memcpy_align4(void *destination,
	const void *source, size_t size);
static u8 video_copy_path(char *destination, u32 size, const char *source);
static Video_Result_t video_map_parser_result(int result);
static Video_Result_t video_map_mjpeg_result(Video_MJPEGResult_t result);
static Video_Result_t video_run_silent(Video_Context_t *context);
static Video_Result_t video_run_pcm(Video_Context_t *context);
static Video_Result_t video_decode_frame(Video_Context_t *context,
	const AVI_Chunk_t *chunk);
static Video_Result_t video_skip_chunk_payload(Video_Context_t *context,
	const AVI_Chunk_t *chunk);
static u32 video_chunk_end(const AVI_Chunk_t *chunk);
static void video_finish(Video_Context_t *context, Video_Result_t result);
static u8 video_wait_launch(Video_Context_t *context);
static u8 video_process_controls(Video_Context_t *context, u32 timeout);
static u8 video_pause_loop(Video_Context_t *context, u32 *deadline);
static u8 video_wait_deadline(Video_Context_t *context, u32 *deadline);
static void video_advance_deadline(Video_Context_t *context, u32 *deadline);
static u8 video_prepare_clock(Video_Context_t *context);
static u8 video_prepare_audio_clock(Video_Context_t *context);
static void video_advance_audio_deadline(Video_Context_t *context);
static Video_Result_t video_pcm_read_audio_payload(Video_Context_t *context,
	const AVI_Chunk_t *chunk, u32 target_bytes, u8 wait_for_space);
static Video_Result_t video_pcm_wait_for_frame(Video_Context_t *context,
	u32 deadline_frames);
static u8 video_pcm_should_drop_frame(Video_Context_t *context,
	u32 deadline_frames);
static u8 video_clear_display(Video_Context_t *context);
static void video_wait_key_release(void);
static void video_update_max(u32 *maximum, u32 value);
static void video_update_mjpeg_stats(Video_Context_t *context);
static void video_update_pcm_stats(Video_Context_t *context);
static void video_update_memory_stats(Video_Context_t *context);
static void video_reset_stats(Video_Context_t *context);
static const char *video_shell_state_text(Video_State_t state);
static void video_shell_fourcc(char *buffer, u32 fourcc);

static Video_Context_t *s_video;
static osStaticThreadDef_t s_video_thread_cb;
static osThreadAttr_t s_video_thread_attr =
{
	.name = "Video",
	.cb_mem = &s_video_thread_cb,
	.cb_size = sizeof(s_video_thread_cb),
	.stack_mem = NULL,
	.stack_size = VIDEO_TASK_STACK_SIZE,
	.priority = (osPriority_t)osPriorityAboveNormal,
};

int Video_Init(void)
{
	Video_Memory_Init();
	return (video_get_context() != NULL) ? SYSTEM_STATUS_OK : OUT_OF_HEAP_MEMORY;
}

int Video_Play(const char *path)
{
	Video_Context_t *context;
	osThreadId_t thread_id;

	Video_Cleanup();
	context = video_get_context();
	if(context == NULL)
	{
		return OUT_OF_HEAP_MEMORY;
	}
	if(path == NULL || context->thread_id != NULL)
	{
		return OPERATE_FAILED;
	}

	memset(context, 0, sizeof(*context));
	if(video_copy_path(context->path, sizeof(context->path), path) == 0U)
	{
		context->result = VIDEO_RESULT_OPEN_FAILED;
		context->state = VIDEO_STATE_ERROR;
		return OPERATE_FAILED;
	}
	context->state = VIDEO_STATE_OPENING;
	context->result = VIDEO_RESULT_OK;
	context->key_previous = KEY_GetState();
	Video_Memory_ResetStats();
	__DMB();

	if(s_video_thread_attr.stack_mem == NULL)
	{
		s_video_thread_attr.stack_mem = sys_thread_stack_alloc(
			(u32)s_video_thread_attr.stack_size);
	}
	if(s_video_thread_attr.stack_mem == NULL)
	{
		context->result = VIDEO_RESULT_NO_MEMORY;
		context->state = VIDEO_STATE_ERROR;
		return OUT_OF_HEAP_MEMORY;
	}

	thread_id = osThreadNew(video_task, context, &s_video_thread_attr);
	if(thread_id == NULL)
	{
		sys_thread_stack_free(s_video_thread_attr.stack_mem);
		s_video_thread_attr.stack_mem = NULL;
		context->result = VIDEO_RESULT_THREAD_FAILED;
		context->state = VIDEO_STATE_ERROR;
		return OPERATE_FAILED;
	}
	context->thread_id = thread_id;
	return SYSTEM_STATUS_OK;
}

int Video_Pause(void)
{
	u32 flags;
	Video_Context_t *context = s_video;

	if(context == NULL || context->thread_id == NULL ||
	   (context->state != VIDEO_STATE_PLAYING &&
	    context->state != VIDEO_STATE_PREBUFFER))
	{
		return OPERATE_FAILED;
	}
	flags = osThreadFlagsSet(context->thread_id, VIDEO_FLAG_PAUSE);
	return ((flags & osFlagsError) == 0U) ? SYSTEM_STATUS_OK : OPERATE_FAILED;
}

int Video_Resume(void)
{
	u32 flags;
	Video_Context_t *context = s_video;

	if(context == NULL || context->thread_id == NULL ||
	   context->state != VIDEO_STATE_PAUSED)
	{
		return OPERATE_FAILED;
	}
	flags = osThreadFlagsSet(context->thread_id, VIDEO_FLAG_RESUME);
	return ((flags & osFlagsError) == 0U) ? SYSTEM_STATUS_OK : OPERATE_FAILED;
}

int Video_TogglePause(void)
{
	return (Video_GetState() == VIDEO_STATE_PAUSED) ? Video_Resume() : Video_Pause();
}

int Video_Stop(void)
{
	u32 flags;
	Video_Context_t *context = s_video;

	Video_Cleanup();
	if(context == NULL || context->thread_id == NULL || context->exit_tick != 0U)
	{
		return SYSTEM_STATUS_OK;
	}
	context->state = VIDEO_STATE_STOPPING;
	flags = osThreadFlagsSet(context->thread_id, VIDEO_FLAG_STOP);
	return ((flags & osFlagsError) == 0U) ? SYSTEM_STATUS_OK : OPERATE_FAILED;
}

void Video_Cleanup(void)
{
	u32 now;
	Video_Context_t *context = s_video;

	if(context == NULL || context->thread_id == NULL || context->exit_tick == 0U)
	{
		return;
	}
	now = osKernelGetTickCount();
	if((now - context->exit_tick) < VIDEO_CLEANUP_DELAY_TICKS)
	{
		return;
	}
	if(s_video_thread_attr.stack_mem != NULL)
	{
		sys_thread_stack_free(s_video_thread_attr.stack_mem);
		s_video_thread_attr.stack_mem = NULL;
	}
	context->thread_id = NULL;
	context->exit_tick = 0U;
}

u8 Video_IsBusy(void)
{
	Video_Cleanup();
	return (s_video != NULL && s_video->thread_id != NULL) ? 1U : 0U;
}

Video_State_t Video_GetState(void)
{
	return (s_video != NULL) ? s_video->state : VIDEO_STATE_IDLE;
}

Video_Result_t Video_GetResult(void)
{
	return (s_video != NULL) ? s_video->result : VIDEO_RESULT_OK;
}

int Video_GetInfo(AVI_Info_t *info)
{
	if(s_video == NULL || info == NULL)
	{
		return OPERATE_FAILED;
	}
	video_memcpy_align4(info, &s_video->info, sizeof(*info));
	return SYSTEM_STATUS_OK;
}

int Video_GetStats(Video_Stats_t *stats)
{
	if(s_video == NULL || stats == NULL)
	{
		return OPERATE_FAILED;
	}
	video_update_pcm_stats(s_video);
	video_update_mjpeg_stats(s_video);
	video_update_memory_stats(s_video);
	if(s_video->thread_id != NULL && s_video->exit_tick == 0U)
	{
		s_video->stats.task_stack_space = osThreadGetStackSpace(s_video->thread_id);
	}
	video_memcpy_align4(stats, &s_video->stats, sizeof(*stats));
	return SYSTEM_STATUS_OK;
}

const char *Video_ResultText(Video_Result_t result)
{
	switch(result)
	{
		case VIDEO_RESULT_OK:
			return "Video ready";
		case VIDEO_RESULT_USER_STOP:
			return "Video stopped";
		case VIDEO_RESULT_EOF:
			return "Video finished";
		case VIDEO_RESULT_OPEN_FAILED:
			return "Open AVI failed";
		case VIDEO_RESULT_UNSUPPORTED:
			return "Unsupported AVI video";
		case VIDEO_RESULT_UNSUPPORTED_AUDIO:
			return "AVI audio must be PCM";
		case VIDEO_RESULT_AUDIO_FAILED:
			return "PCM audio failed";
		case VIDEO_RESULT_BAD_CONTAINER:
			return "Invalid AVI container";
		case VIDEO_RESULT_DECODE_FAILED:
			return "MJPEG decode failed";
		case VIDEO_RESULT_IO_FAILED:
			return "AVI read failed";
		case VIDEO_RESULT_NO_MEMORY:
			return "Video memory failed";
		case VIDEO_RESULT_DISPLAY_BUSY:
			return "Display is busy";
		case VIDEO_RESULT_DISPLAY_FAILED:
			return "LCD transfer failed";
		case VIDEO_RESULT_DISPLAY_RESUME_FAILED:
			return "LVGL resume failed";
		case VIDEO_RESULT_THREAD_FAILED:
			return "Video task failed";
		default:
			return "Video error";
	}
}

static void video_task(void *argument)
{
	Video_Context_t *context = (Video_Context_t *)argument;
	Video_Result_t final_result;
	int pcm_result;
	int parser_result;

	if(context == NULL)
	{
		osThreadExit();
		return;
	}
	context->thread_id = osThreadGetId();
	if(video_wait_launch(context) == 0U)
	{
		video_finish(context, VIDEO_RESULT_USER_STOP);
		return;
	}

	context->state = VIDEO_STATE_OPENING;
	if(f_open(&context->file, (const TCHAR *)context->path, FA_READ) != FR_OK)
	{
		video_finish(context, VIDEO_RESULT_OPEN_FAILED);
		return;
	}
	context->file_opened = 1U;
	parser_result = AVI_Parser_Open(&context->parser, &context->file);
	if(parser_result != AVI_PARSER_OK)
	{
		video_finish(context, video_map_parser_result(parser_result));
		return;
	}
	video_memcpy_align4(&context->info, &context->parser.info,
	                    sizeof(context->info));
	if(context->info.has_audio != 0U && context->info.audio_supported == 0U)
	{
		video_finish(context, VIDEO_RESULT_UNSUPPORTED_AUDIO);
		return;
	}
	if((context->info.has_audio != 0U && video_prepare_audio_clock(context) == 0U) ||
	   (context->info.has_audio == 0U && video_prepare_clock(context) == 0U))
	{
		video_finish(context, (context->info.has_audio != 0U) ?
		             VIDEO_RESULT_UNSUPPORTED_AUDIO : VIDEO_RESULT_UNSUPPORTED);
		return;
	}

	context->state = VIDEO_STATE_PREBUFFER;
	if(Video_MJPEG_Init(&context->mjpeg) != 0)
	{
		video_finish(context, VIDEO_RESULT_NO_MEMORY);
		return;
	}
	context->mjpeg_opened = 1U;
	if(context->info.has_audio != 0U)
	{
		pcm_result = Video_PCM_Open(&context->pcm, &context->info);
		if(pcm_result != VIDEO_PCM_OK)
		{
			video_finish(context, (pcm_result == VIDEO_PCM_MEMORY_ERROR) ?
			             VIDEO_RESULT_NO_MEMORY : VIDEO_RESULT_AUDIO_FAILED);
			return;
		}
		context->pcm_opened = 1U;
	}
	if(lvgl_stop_ui() != SYSTEM_STATUS_OK)
	{
		video_finish(context, VIDEO_RESULT_DISPLAY_BUSY);
		return;
	}
	context->lcd_owned = 1U;
	if(video_clear_display(context) == 0U)
	{
		video_finish(context, VIDEO_RESULT_DISPLAY_FAILED);
		return;
	}

	final_result = (context->info.has_audio != 0U) ?
	               video_run_pcm(context) : video_run_silent(context);

	video_finish(context, final_result);
}

static Video_Result_t video_run_silent(Video_Context_t *context)
{
	AVI_Chunk_t chunk;
	Video_Result_t decode_result;
	u32 deadline = osKernelGetTickCount();
	u32 drop_threshold;
	u32 now;
	u8 control;
	int parser_result;

	drop_threshold = context->deadline_whole_ticks +
		((context->deadline_fraction_ticks != 0U) ? 1U : 0U);
	if(drop_threshold == 0U)
	{
		drop_threshold = 1U;
	}
	context->state = VIDEO_STATE_PLAYING;
	while(1)
	{
		control = video_process_controls(context, 0U);
		if(control == VIDEO_CONTROL_STOP)
		{
			return VIDEO_RESULT_USER_STOP;
		}
		if(context->state == VIDEO_STATE_PAUSED ||
		   control == VIDEO_CONTROL_PAUSE)
		{
			control = video_pause_loop(context, &deadline);
			if(control == VIDEO_CONTROL_STOP)
			{
				return VIDEO_RESULT_USER_STOP;
			}
			if(control == VIDEO_CONTROL_ERROR)
			{
				return VIDEO_RESULT_AUDIO_FAILED;
			}
		}

		parser_result = AVI_Parser_Next(&context->parser, &chunk);
		if(parser_result == AVI_PARSER_EOF)
		{
			return VIDEO_RESULT_EOF;
		}
		if(parser_result != AVI_PARSER_OK)
		{
			return video_map_parser_result(parser_result);
		}
		if(chunk.type != AVI_CHUNK_VIDEO)
		{
			continue;
		}
		context->stats.video_chunks++;

		if(video_wait_deadline(context, &deadline) == VIDEO_CONTROL_STOP)
		{
			return VIDEO_RESULT_USER_STOP;
		}
		now = osKernelGetTickCount();
		if((s32)(now - deadline) > (s32)drop_threshold)
		{
			context->stats.video_frames_dropped++;
			video_advance_deadline(context, &deadline);
			continue;
		}

		decode_result = video_decode_frame(context, &chunk);
		video_advance_deadline(context, &deadline);
		if(decode_result != VIDEO_RESULT_OK)
		{
			return decode_result;
		}
	}
}

static Video_Result_t video_run_pcm(Video_Context_t *context)
{
	Video_Result_t error = VIDEO_RESULT_OK;
	AVI_Chunk_t chunk;
	u8 control;
	int start_result;
	int parser_result;
	u8 first_frame_displayed = 0U;
	u32 target_bytes;

	while(context->demux_eof == 0U &&
	      (first_frame_displayed == 0U ||
	       Video_PCM_GetBufferedBytes(&context->pcm) < context->pcm.prebuffer_bytes))
	{
		control = video_process_controls(context, 0U);
		if(control == VIDEO_CONTROL_STOP)
		{
			return VIDEO_RESULT_USER_STOP;
		}
		if(control == VIDEO_CONTROL_PAUSE)
		{
			control = video_pause_loop(context, NULL);
			if(control == VIDEO_CONTROL_STOP)
			{
				return VIDEO_RESULT_USER_STOP;
			}
		}

		if(context->active_audio_pending != 0U)
		{
			target_bytes = (first_frame_displayed != 0U) ?
				context->pcm.prebuffer_bytes : 0U;
			error = video_pcm_read_audio_payload(context, NULL,
				target_bytes, 0U);
			if(error != VIDEO_RESULT_OK)
			{
				return error;
			}
			if(context->active_audio_pending != 0U)
			{
				if(first_frame_displayed != 0U &&
				   Video_PCM_GetBufferedBytes(&context->pcm) >=
				   context->pcm.prebuffer_bytes)
				{
					break;
				}
				return VIDEO_RESULT_AUDIO_FAILED;
			}
			continue;
		}

		parser_result = AVI_Parser_Next(&context->parser, &chunk);
		if(parser_result == AVI_PARSER_EOF)
		{
			context->demux_eof = 1U;
			Video_PCM_SetInputEof(&context->pcm);
			break;
		}
		if(parser_result != AVI_PARSER_OK)
		{
			return video_map_parser_result(parser_result);
		}
		if(chunk.type == AVI_CHUNK_AUDIO)
		{
			context->stats.audio_chunks++;
			target_bytes = (first_frame_displayed != 0U) ?
				context->pcm.prebuffer_bytes : 0U;
			error = video_pcm_read_audio_payload(context, &chunk,
				target_bytes, 0U);
			if(error != VIDEO_RESULT_OK)
			{
				return error;
			}
			continue;
		}
		if(chunk.type != AVI_CHUNK_VIDEO)
		{
			error = video_skip_chunk_payload(context, &chunk);
			if(error != VIDEO_RESULT_OK)
			{
				return error;
			}
			continue;
		}

		context->stats.video_chunks++;
		if(first_frame_displayed == 0U)
		{
			error = video_decode_frame(context, &chunk);
			if(error == VIDEO_RESULT_OK)
			{
				error = video_skip_chunk_payload(context, &chunk);
			}
			if(error != VIDEO_RESULT_OK)
			{
				return error;
			}
			video_advance_audio_deadline(context);
			first_frame_displayed = 1U;
			continue;
		}

		context->stats.video_frames_dropped++;
		video_advance_audio_deadline(context);
		error = video_skip_chunk_payload(context, &chunk);
		if(error != VIDEO_RESULT_OK)
		{
			return error;
		}
	}
	if(Video_PCM_GetBufferedBytes(&context->pcm) == 0U)
	{
		return VIDEO_RESULT_BAD_CONTAINER;
	}
	start_result = Video_PCM_Start(&context->pcm);
	if(start_result != VIDEO_PCM_OK)
	{
		return (start_result == VIDEO_PCM_WOULD_BLOCK) ?
		       VIDEO_RESULT_BAD_CONTAINER : VIDEO_RESULT_AUDIO_FAILED;
	}
	context->state = VIDEO_STATE_PLAYING;

	while(1)
	{
		if(Video_PCM_CheckHealth(&context->pcm) != VIDEO_PCM_OK)
		{
			return VIDEO_RESULT_AUDIO_FAILED;
		}
		control = video_process_controls(context, 0U);
		if(control == VIDEO_CONTROL_STOP)
		{
			return VIDEO_RESULT_USER_STOP;
		}
		if(context->state == VIDEO_STATE_PAUSED ||
		   control == VIDEO_CONTROL_PAUSE)
		{
			control = video_pause_loop(context, NULL);
			if(control == VIDEO_CONTROL_STOP)
			{
				return VIDEO_RESULT_USER_STOP;
			}
			if(control == VIDEO_CONTROL_ERROR)
			{
				return VIDEO_RESULT_AUDIO_FAILED;
			}
		}

		if(context->active_audio_pending != 0U)
		{
			error = video_pcm_read_audio_payload(context, NULL, 0U, 1U);
			if(error != VIDEO_RESULT_OK)
			{
				return error;
			}
			continue;
		}
		if(context->demux_eof != 0U)
		{
			if(Video_PCM_IsDrained(&context->pcm) != 0U)
			{
				return VIDEO_RESULT_EOF;
			}
			control = video_process_controls(context, VIDEO_CONTROL_POLL_TICKS);
			if(control == VIDEO_CONTROL_STOP)
			{
				return VIDEO_RESULT_USER_STOP;
			}
			if(control == VIDEO_CONTROL_PAUSE)
			{
				control = video_pause_loop(context, NULL);
				if(control == VIDEO_CONTROL_STOP)
				{
					return VIDEO_RESULT_USER_STOP;
				}
				if(control == VIDEO_CONTROL_ERROR)
				{
					return VIDEO_RESULT_AUDIO_FAILED;
				}
			}
			continue;
		}

		parser_result = AVI_Parser_Next(&context->parser, &chunk);
		if(parser_result == AVI_PARSER_EOF)
		{
			context->demux_eof = 1U;
			Video_PCM_SetInputEof(&context->pcm);
			continue;
		}
		if(parser_result != AVI_PARSER_OK)
		{
			return video_map_parser_result(parser_result);
		}

		if(chunk.type == AVI_CHUNK_AUDIO)
		{
			context->stats.audio_chunks++;
			error = video_pcm_read_audio_payload(context, &chunk, 0U, 1U);
			if(error != VIDEO_RESULT_OK)
			{
				return error;
			}
			continue;
		}
		if(chunk.type != AVI_CHUNK_VIDEO)
		{
			error = video_skip_chunk_payload(context, &chunk);
			if(error != VIDEO_RESULT_OK)
			{
				return error;
			}
			continue;
		}

		context->stats.video_chunks++;
		error = video_pcm_wait_for_frame(context, context->audio_deadline_frames);
		if(error != VIDEO_RESULT_OK)
		{
			return error;
		}
		if(video_pcm_should_drop_frame(context, context->audio_deadline_frames) != 0U)
		{
			context->stats.video_frames_dropped++;
			video_advance_audio_deadline(context);
			error = video_skip_chunk_payload(context, &chunk);
			if(error != VIDEO_RESULT_OK)
			{
				return error;
			}
			continue;
		}

		error = video_decode_frame(context, &chunk);
		video_advance_audio_deadline(context);
		if(error == VIDEO_RESULT_OK)
		{
			error = video_skip_chunk_payload(context, &chunk);
		}
		if(error != VIDEO_RESULT_OK)
		{
			return error;
		}
	}
}

static Video_Result_t video_decode_frame(Video_Context_t *context,
	const AVI_Chunk_t *chunk)
{
	Video_MJPEGResult_t decode_result;
	u32 decode_start;

	decode_start = osKernelGetTickCount();
	decode_result = Video_MJPEG_DecodeFrame(&context->mjpeg, &context->file,
		chunk, context->info.width, context->info.height);
	video_update_max(&context->stats.max_jpeg_decode_ticks,
	                 osKernelGetTickCount() - decode_start);
	if(decode_result == VIDEO_MJPEG_OK)
	{
		context->stats.video_frames_decoded++;
		context->stats.video_frames_displayed++;
		context->consecutive_jpeg_errors = 0U;
		return VIDEO_RESULT_OK;
	}

	context->stats.jpeg_errors++;
	if(decode_result == VIDEO_MJPEG_FORMAT_ERROR &&
	   context->consecutive_jpeg_errors < VIDEO_MAX_CONSECUTIVE_JPEG_ERRORS)
	{
		context->consecutive_jpeg_errors++;
		return VIDEO_RESULT_OK;
	}
	return video_map_mjpeg_result(decode_result);
}

static Video_Result_t video_skip_chunk_payload(Video_Context_t *context,
	const AVI_Chunk_t *chunk)
{
	u32 end;

	if(context == NULL || chunk == NULL)
	{
		return VIDEO_RESULT_BAD_CONTAINER;
	}
	end = video_chunk_end(chunk);
	if((u32)f_tell(&context->file) != end &&
	   f_lseek(&context->file, (FSIZE_t)end) != FR_OK)
	{
		return VIDEO_RESULT_IO_FAILED;
	}
	return VIDEO_RESULT_OK;
}

static u32 video_chunk_end(const AVI_Chunk_t *chunk)
{
	u32 padded_size;

	if(chunk == NULL)
	{
		return 0U;
	}
	padded_size = chunk->data_size + (chunk->data_size & 1U);
	return chunk->data_offset + padded_size;
}

static Video_Result_t video_pcm_read_audio_payload(Video_Context_t *context,
	const AVI_Chunk_t *chunk, u32 target_bytes, u8 wait_for_space)
{
	u8 control;
	int result;

	if(context == NULL)
	{
		return VIDEO_RESULT_BAD_CONTAINER;
	}
	if(chunk != NULL)
	{
		if(context->active_audio_pending != 0U ||
		   (chunk->data_size % context->pcm.block_align) != 0U)
		{
			return VIDEO_RESULT_BAD_CONTAINER;
		}
		context->active_audio_offset = chunk->data_offset;
		context->active_audio_remaining = chunk->data_size;
		context->active_audio_end = video_chunk_end(chunk);
		context->active_audio_pending = (chunk->data_size != 0U) ? 1U : 0U;
		if(context->active_audio_pending == 0U)
		{
			return video_skip_chunk_payload(context, chunk);
		}
	}

	while(context->active_audio_pending != 0U)
	{
		if(target_bytes != 0U &&
		   Video_PCM_GetBufferedBytes(&context->pcm) >= target_bytes)
		{
			return VIDEO_RESULT_OK;
		}
		result = Video_PCM_ReadChunkPart(&context->pcm, &context->file,
			&context->active_audio_offset, &context->active_audio_remaining);
		if(result == VIDEO_PCM_WOULD_BLOCK)
		{
			if(wait_for_space == 0U)
			{
				return VIDEO_RESULT_OK;
			}
			if(Video_PCM_CheckHealth(&context->pcm) != VIDEO_PCM_OK)
			{
				return VIDEO_RESULT_AUDIO_FAILED;
			}
			control = video_process_controls(context, VIDEO_PCM_WAIT_TICKS);
			if(control == VIDEO_CONTROL_STOP)
			{
				return VIDEO_RESULT_USER_STOP;
			}
			if(control == VIDEO_CONTROL_PAUSE)
			{
				control = video_pause_loop(context, NULL);
				if(control == VIDEO_CONTROL_STOP)
				{
					return VIDEO_RESULT_USER_STOP;
				}
				if(control == VIDEO_CONTROL_ERROR)
				{
					return VIDEO_RESULT_AUDIO_FAILED;
				}
			}
			continue;
		}
		if(result != VIDEO_PCM_OK)
		{
			return (result == VIDEO_PCM_IO_ERROR) ?
				VIDEO_RESULT_IO_FAILED : VIDEO_RESULT_BAD_CONTAINER;
		}
		if(context->active_audio_remaining == 0U)
		{
			context->active_audio_pending = 0U;
			if((u32)f_tell(&context->file) != context->active_audio_end &&
			   f_lseek(&context->file,
			           (FSIZE_t)context->active_audio_end) != FR_OK)
			{
				return VIDEO_RESULT_IO_FAILED;
			}
		}
	}
	return VIDEO_RESULT_OK;
}

static Video_Result_t video_pcm_wait_for_frame(Video_Context_t *context,
	u32 deadline_frames)
{
	u8 control;

	if(context == NULL)
	{
		return VIDEO_RESULT_BAD_CONTAINER;
	}
	while((s32)(Video_PCM_GetPlayedFrames(&context->pcm) - deadline_frames) < 0)
	{
		if(Video_PCM_CheckHealth(&context->pcm) != VIDEO_PCM_OK)
		{
			return VIDEO_RESULT_AUDIO_FAILED;
		}
		control = video_process_controls(context, VIDEO_PCM_WAIT_TICKS);
		if(control == VIDEO_CONTROL_STOP)
		{
			return VIDEO_RESULT_USER_STOP;
		}
		if(control == VIDEO_CONTROL_PAUSE)
		{
			control = video_pause_loop(context, NULL);
			if(control == VIDEO_CONTROL_STOP)
			{
				return VIDEO_RESULT_USER_STOP;
			}
			if(control == VIDEO_CONTROL_ERROR)
			{
				return VIDEO_RESULT_AUDIO_FAILED;
			}
		}
	}
	return VIDEO_RESULT_OK;
}

static u8 video_pcm_should_drop_frame(Video_Context_t *context,
	u32 deadline_frames)
{
	u32 played_frames;
	u32 interval_frames;

	if(context == NULL)
	{
		return 0U;
	}
	interval_frames = context->audio_deadline_whole_frames;
	if(context->audio_deadline_fraction_frames != 0U)
	{
		interval_frames++;
	}
	if(interval_frames == 0U)
	{
		interval_frames = 1U;
	}
	played_frames = Video_PCM_GetPlayedFrames(&context->pcm);
	return ((s32)(played_frames - deadline_frames) >=
	        (s32)interval_frames) ? 1U : 0U;
}

static Video_Context_t *video_get_context(void)
{
	if(s_video == NULL)
	{
		s_video = (Video_Context_t *)Video_Memory_Alloc(sizeof(Video_Context_t));
		if(s_video != NULL)
		{
			memset(s_video, 0, sizeof(*s_video));
		}
	}
	return s_video;
}

static __attribute__((noinline)) void video_memcpy_align4(void *destination,
	const void *source, size_t size)
{
	/* AVI-only helper; both addresses must be aligned to four bytes. */
	u32 *destination_words = (u32 *)destination;
	const u32 *source_words = (const u32 *)source;
	u32 word_count = (u32)(size >> 2U);
	u32 remaining;
	u32 index;
	u32 data0;
	u32 data1;
	u32 data2;
	u32 data3;

	while(word_count >= 4U)
	{
		data0 = source_words[0];
		data1 = source_words[1];
		data2 = source_words[2];
		data3 = source_words[3];
		source_words += 4;

		destination_words[0] = data0;
		destination_words[1] = data1;
		destination_words[2] = data2;
		destination_words[3] = data3;
		destination_words += 4;
		word_count -= 4U;
	}

	remaining = (word_count << 2U) + ((u32)size & 0x03U);
	for(index = 0U; index < remaining; index++)
	{
		((u8 *)destination_words)[index] = ((const u8 *)source_words)[index];
	}
}

static u8 video_copy_path(char *destination, u32 size, const char *source)
{
	u32 index;

	if(destination == NULL || source == NULL || size == 0U)
	{
		return 0U;
	}
	for(index = 0U; index < (size - 1U); index++)
	{
		destination[index] = source[index];
		if(source[index] == '\0')
		{
			return 1U;
		}
	}
	destination[size - 1U] = '\0';
	return 0U;
}

static Video_Result_t video_map_parser_result(int result)
{
	switch(result)
	{
		case AVI_PARSER_IO_ERROR:
			return VIDEO_RESULT_IO_FAILED;
		case AVI_PARSER_UNSUPPORTED:
			return VIDEO_RESULT_UNSUPPORTED;
		case AVI_PARSER_BAD_CONTAINER:
		case AVI_PARSER_BAD_CHUNK:
			return VIDEO_RESULT_BAD_CONTAINER;
		default:
			return VIDEO_RESULT_BAD_CONTAINER;
	}
}

static Video_Result_t video_map_mjpeg_result(Video_MJPEGResult_t result)
{
	switch(result)
	{
		case VIDEO_MJPEG_IO_ERROR:
			return VIDEO_RESULT_IO_FAILED;
		case VIDEO_MJPEG_MEMORY_ERROR:
			return VIDEO_RESULT_NO_MEMORY;
		case VIDEO_MJPEG_DISPLAY_ERROR:
			return VIDEO_RESULT_DISPLAY_FAILED;
		case VIDEO_MJPEG_SIZE_ERROR:
			return VIDEO_RESULT_UNSUPPORTED;
		default:
			return VIDEO_RESULT_DECODE_FAILED;
	}
}

static void video_finish(Video_Context_t *context, Video_Result_t result)
{
	if(context == NULL)
	{
		osThreadExit();
		return;
	}
	context->state = VIDEO_STATE_STOPPING;
	if(context->pcm_opened != 0U)
	{
		video_update_pcm_stats(context);
		Video_PCM_Close(&context->pcm);
		context->pcm_opened = 0U;
	}
	if(context->mjpeg_opened != 0U)
	{
		video_update_mjpeg_stats(context);
		Video_MJPEG_Close(&context->mjpeg);
		context->mjpeg_opened = 0U;
	}
	if(context->file_opened != 0U)
	{
		(void)f_close(&context->file);
		context->file_opened = 0U;
	}
	if(context->lcd_owned != 0U)
	{
		(void)LCD_StreamWait();
		LCD_StreamEnd();
		video_wait_key_release();
		if(lvgl_start_ui() != SYSTEM_STATUS_OK)
		{
			result = VIDEO_RESULT_DISPLAY_RESUME_FAILED;
		}
		context->lcd_owned = 0U;
	}

	context->stats.task_stack_space = osThreadGetStackSpace(osThreadGetId());
	video_update_memory_stats(context);
	context->result = result;
	context->state = (result == VIDEO_RESULT_EOF || result == VIDEO_RESULT_USER_STOP ||
	                  result == VIDEO_RESULT_OK) ? VIDEO_STATE_FINISHED : VIDEO_STATE_ERROR;
	__DMB();
	context->exit_tick = osKernelGetTickCount();
	__DMB();
	osThreadExit();
}

static u8 video_wait_launch(Video_Context_t *context)
{
	u32 elapsed = 0U;
	u32 flags;

	while(elapsed < VIDEO_LAUNCH_DELAY_TICKS)
	{
		flags = osThreadFlagsWait(VIDEO_FLAG_STOP, osFlagsWaitAny,
			VIDEO_CONTROL_POLL_TICKS);
		if((flags & osFlagsError) == 0U && (flags & VIDEO_FLAG_STOP) != 0U)
		{
			return 0U;
		}
		elapsed += VIDEO_CONTROL_POLL_TICKS;
		context->key_previous = KEY_GetState();
	}
	return 1U;
}

static u8 video_process_controls(Video_Context_t *context, u32 timeout)
{
	u32 flags;
	u32 keys;
	u32 pressed;

	flags = osThreadFlagsWait(VIDEO_FLAG_THREAD_MASK, osFlagsWaitAny, timeout);
	if((flags & osFlagsError) != 0U)
	{
		flags = 0U;
	}
	flags &= VIDEO_FLAG_CONTROL_MASK;
	keys = KEY_GetState();
	pressed = keys & ~context->key_previous;
	context->key_previous = keys;

	if((flags & VIDEO_FLAG_STOP) != 0U ||
	   (pressed & (KEY_GAME_EXIT | KEY_GAME_B)) != 0U)
	{
		return VIDEO_CONTROL_STOP;
	}
	if(context->state == VIDEO_STATE_PAUSED)
	{
		if((flags & VIDEO_FLAG_RESUME) != 0U || (pressed & KEY_GAME_OK) != 0U)
		{
			return VIDEO_CONTROL_RESUME;
		}
	}
	else if((flags & VIDEO_FLAG_PAUSE) != 0U || (pressed & KEY_GAME_OK) != 0U)
	{
		return VIDEO_CONTROL_PAUSE;
	}
	return VIDEO_CONTROL_NONE;
}

static u8 video_pause_loop(Video_Context_t *context, u32 *deadline)
{
	u32 pause_start = osKernelGetTickCount();
	Video_State_t resume_state = context->state;
	u8 control;

	if(resume_state == VIDEO_STATE_PAUSED)
	{
		resume_state = VIDEO_STATE_PLAYING;
	}
	if(context->pcm_opened != 0U && context->pcm.started != 0U)
	{
		if(Video_PCM_Pause(&context->pcm) != VIDEO_PCM_OK)
		{
			return VIDEO_CONTROL_ERROR;
		}
	}
	context->state = VIDEO_STATE_PAUSED;
	while(1)
	{
		control = video_process_controls(context, VIDEO_CONTROL_POLL_TICKS);
		if(control == VIDEO_CONTROL_STOP)
		{
			return VIDEO_CONTROL_STOP;
		}
		if(control == VIDEO_CONTROL_RESUME)
		{
			if(context->pcm_opened != 0U && context->pcm.started != 0U &&
			   Video_PCM_Resume(&context->pcm) != VIDEO_PCM_OK)
			{
				return VIDEO_CONTROL_ERROR;
			}
			if(deadline != NULL)
			{
				*deadline += osKernelGetTickCount() - pause_start;
			}
			context->state = resume_state;
			return VIDEO_CONTROL_RESUME;
		}
	}
}

static u8 video_wait_deadline(Video_Context_t *context, u32 *deadline)
{
	u32 now;
	u32 wait_ticks;
	u8 control;

	while((s32)(osKernelGetTickCount() - *deadline) < 0)
	{
		now = osKernelGetTickCount();
		wait_ticks = *deadline - now;
		if(wait_ticks > VIDEO_CONTROL_POLL_TICKS)
		{
			wait_ticks = VIDEO_CONTROL_POLL_TICKS;
		}
		control = video_process_controls(context, wait_ticks);
		if(control == VIDEO_CONTROL_STOP)
		{
			return VIDEO_CONTROL_STOP;
		}
		if(control == VIDEO_CONTROL_PAUSE)
		{
			control = video_pause_loop(context, deadline);
			if(control == VIDEO_CONTROL_STOP)
			{
				return VIDEO_CONTROL_STOP;
			}
		}
	}
	return VIDEO_CONTROL_NONE;
}

static void video_advance_deadline(Video_Context_t *context, u32 *deadline)
{
	*deadline += context->deadline_whole_ticks;
	if(context->deadline_fraction_ticks == 0U)
	{
		return;
	}
	if(context->deadline_fraction_accumulator >=
	   (context->info.video_rate - context->deadline_fraction_ticks))
	{
		context->deadline_fraction_accumulator -=
			(context->info.video_rate - context->deadline_fraction_ticks);
		(*deadline)++;
	}
	else
	{
		context->deadline_fraction_accumulator += context->deadline_fraction_ticks;
	}
}

static u8 video_prepare_clock(Video_Context_t *context)
{
	u32 tick_frequency = osKernelGetTickFreq();
	u32 numerator;

	if(tick_frequency == 0U || context->info.video_scale == 0U ||
	   context->info.video_rate == 0U ||
	   context->info.video_scale > (0xFFFFFFFFUL / tick_frequency))
	{
		return 0U;
	}
	numerator = context->info.video_scale * tick_frequency;
	context->deadline_whole_ticks = numerator / context->info.video_rate;
	context->deadline_fraction_ticks = numerator % context->info.video_rate;
	context->deadline_fraction_accumulator = 0U;
	return (context->deadline_whole_ticks != 0U ||
	        context->deadline_fraction_ticks != 0U) ? 1U : 0U;
}

static u8 video_prepare_audio_clock(Video_Context_t *context)
{
	u32 numerator;

	if(context == NULL || context->info.audio_sample_rate == 0U ||
	   context->info.video_scale == 0U || context->info.video_rate == 0U ||
	   context->info.video_scale >
	   (0xFFFFFFFFUL / context->info.audio_sample_rate))
	{
		return 0U;
	}

	numerator = context->info.video_scale * context->info.audio_sample_rate;
	context->audio_deadline_frames = 0U;
	context->audio_deadline_whole_frames = numerator / context->info.video_rate;
	context->audio_deadline_fraction_frames = numerator % context->info.video_rate;
	context->audio_deadline_fraction_accumulator = 0U;
	return (context->audio_deadline_whole_frames != 0U ||
	        context->audio_deadline_fraction_frames != 0U) ? 1U : 0U;
}

static void video_advance_audio_deadline(Video_Context_t *context)
{
	context->audio_deadline_frames += context->audio_deadline_whole_frames;
	if(context->audio_deadline_fraction_frames == 0U)
	{
		return;
	}
	if(context->audio_deadline_fraction_accumulator >=
	   (context->info.video_rate - context->audio_deadline_fraction_frames))
	{
		context->audio_deadline_fraction_accumulator -=
			(context->info.video_rate - context->audio_deadline_fraction_frames);
		context->audio_deadline_frames++;
	}
	else
	{
		context->audio_deadline_fraction_accumulator +=
			context->audio_deadline_fraction_frames;
	}
}

static u8 video_clear_display(Video_Context_t *context)
{
	u32 stripe_pixels = VIDEO_MJPEG_MAX_WIDTH * VIDEO_MJPEG_STRIPE_LINES;
	u32 total_pixels = VIDEO_MJPEG_MAX_WIDTH * 240U;
	u32 offset;

	memset(context->mjpeg.stripe[0], 0, stripe_pixels * sizeof(u16));
	if(LCD_StreamBegin(0U, 0U, VIDEO_MJPEG_MAX_WIDTH, 240U) != 0U)
	{
		return 0U;
	}
	for(offset = 0U; offset < total_pixels; offset += stripe_pixels)
	{
		if(LCD_StreamWriteAsync(context->mjpeg.stripe[0], stripe_pixels) != 0U)
		{
			LCD_StreamEnd();
			context->stats.lcd_dma_timeouts++;
			return 0U;
		}
	}
	if(LCD_StreamWait() != 0U)
	{
		LCD_StreamEnd();
		context->stats.lcd_dma_timeouts++;
		return 0U;
	}
	LCD_StreamEnd();
	return 1U;
}

static void video_wait_key_release(void)
{
	u32 waited = 0U;
	u32 mask = KEY_GAME_OK | KEY_GAME_B | KEY_GAME_EXIT;

	while((KEY_GetRawState() & mask) != 0U && waited < VIDEO_KEY_RELEASE_TIMEOUT_TICKS)
	{
		(void)osDelay(VIDEO_CONTROL_POLL_TICKS);
		waited += VIDEO_CONTROL_POLL_TICKS;
	}
}

static void video_update_max(u32 *maximum, u32 value)
{
	if(maximum != NULL && value > *maximum)
	{
		*maximum = value;
	}
}

static void video_update_mjpeg_stats(Video_Context_t *context)
{
	if(context == NULL || context->mjpeg_opened == 0U)
	{
		return;
	}
	context->stats.jpeg_eoi_injected = context->mjpeg.jpeg_eoi_injected;
	context->stats.fatfs_read_errors = context->mjpeg.fatfs_read_errors +
		context->pcm_fatfs_read_errors;
	context->stats.fatfs_read_calls = context->mjpeg.fatfs_read_calls +
		context->pcm_fatfs_read_calls;
	context->stats.fatfs_read_bytes = context->mjpeg.fatfs_read_bytes +
		context->pcm_fatfs_read_bytes;
	context->stats.lcd_wait_ticks = context->mjpeg.lcd_wait_ticks;
	context->stats.max_lcd_wait_ticks = context->mjpeg.max_lcd_wait_ticks;
	context->stats.jpeg_arena_peak_bytes = context->mjpeg.jpeg_arena_peak_bytes;
	context->stats.jpeg_arena_allocation_failures =
		context->mjpeg.jpeg_arena_allocation_failures;
}

static void video_update_pcm_stats(Video_Context_t *context)
{
	Video_PCMStats_t pcm_stats;

	if(context == NULL || context->pcm_opened == 0U)
	{
		return;
	}
	Video_PCM_GetStats(&context->pcm, &pcm_stats);
	context->stats.pcm_input_bytes = pcm_stats.input_bytes;
	context->stats.pcm_output_frames = pcm_stats.output_frames;
	context->stats.audio_fifo_bytes = Video_PCM_GetBufferedBytes(&context->pcm);
	context->stats.audio_underruns = pcm_stats.underruns;
	context->stats.audio_fifo_peak_bytes = pcm_stats.fifo_peak_bytes;
	context->stats.audio_dma_events = pcm_stats.dma_events;
	context->stats.audio_dma_flag_collisions = pcm_stats.dma_flag_collisions;
	context->stats.audio_refill_events = pcm_stats.refill_events;
	context->stats.audio_refill_missed = pcm_stats.refill_missed;
	context->stats.audio_refill_queue_peak = pcm_stats.refill_queue_peak;
	context->stats.audio_media_frames = pcm_stats.media_frames;
	context->stats.audio_replayed_frames = pcm_stats.replayed_frames;
	context->stats.audio_silence_frames = pcm_stats.silence_frames;
	context->stats.audio_dma_half_events = pcm_stats.dma_half_events;
	context->stats.audio_dma_full_events = pcm_stats.dma_full_events;
	context->stats.audio_dma_errors = pcm_stats.dma_errors;
	context->stats.audio_i2s_errors = pcm_stats.i2s_errors;
	context->stats.audio_i2s_underruns = pcm_stats.i2s_underruns;
	context->stats.audio_late_writes = pcm_stats.late_writes;
	context->stats.audio_last_error = pcm_stats.last_audio_error;
	context->stats.audio_last_i2s_status = pcm_stats.last_i2s_status;
	context->pcm_fatfs_read_errors = pcm_stats.fatfs_read_errors;
	context->pcm_fatfs_read_calls = pcm_stats.fatfs_read_calls;
	context->pcm_fatfs_read_bytes = pcm_stats.fatfs_read_bytes;
	context->stats.pending_video_frames = 0U;
	context->stats.pending_audio_chunks =
		(context->active_audio_pending != 0U) ? 1U : 0U;
	context->stats.audio_prebuffer_bytes = context->pcm.prebuffer_bytes;
	context->stats.audio_deadline_frames = context->audio_deadline_frames;
}

static void video_update_memory_stats(Video_Context_t *context)
{
	Video_MemoryStats_t memory_stats;

	if(context == NULL)
	{
		return;
	}
	Video_Memory_GetStats(&memory_stats);
	context->stats.memory_pool_size = memory_stats.pool_size;
	context->stats.memory_current_bytes = memory_stats.current_bytes;
	context->stats.memory_peak_bytes = memory_stats.peak_bytes;
	context->stats.memory_largest_free_block = memory_stats.largest_free_block;
	context->stats.memory_allocation_failures = memory_stats.allocation_failures;
}

static void video_reset_stats(Video_Context_t *context)
{
	if(context == NULL)
	{
		return;
	}

	memset(&context->stats, 0, sizeof(context->stats));
	context->pcm_fatfs_read_errors = 0U;
	context->pcm_fatfs_read_calls = 0U;
	context->pcm_fatfs_read_bytes = 0U;
	context->mjpeg.jpeg_eoi_injected = 0U;
	context->mjpeg.fatfs_read_errors = 0U;
	context->mjpeg.fatfs_read_calls = 0U;
	context->mjpeg.fatfs_read_bytes = 0U;
	context->mjpeg.lcd_wait_ticks = 0U;
	context->mjpeg.max_lcd_wait_ticks = 0U;
	context->mjpeg.jpeg_arena_peak_bytes = 0U;
	context->mjpeg.jpeg_arena_allocation_failures = 0U;
	context->pcm.input_bytes = 0U;
	context->pcm.fifo_peak_bytes = Video_PCM_GetBufferedBytes(&context->pcm);
	context->pcm.underruns = 0U;
	context->pcm.dma_events = 0U;
	context->pcm.dma_flag_collisions = 0U;
	context->pcm.refill_events = 0U;
	context->pcm.refill_missed = 0U;
	context->pcm.refill_queue_peak = 0U;
	context->pcm.media_frames = 0U;
	context->pcm.replayed_frames = 0U;
	context->pcm.silence_frames = 0U;
	context->pcm.fatfs_read_errors = 0U;
	context->pcm.fatfs_read_calls = 0U;
	context->pcm.fatfs_read_bytes = 0U;
	if(context->pcm_opened != 0U)
	{
		audio_card_reset_stats();
	}
	Video_Memory_ResetStats();
}

static const char *video_shell_state_text(Video_State_t state)
{
	switch(state)
	{
		case VIDEO_STATE_IDLE:
			return "IDLE";
		case VIDEO_STATE_OPENING:
			return "OPENING";
		case VIDEO_STATE_PREBUFFER:
			return "PREBUFFER";
		case VIDEO_STATE_PLAYING:
			return "PLAYING";
		case VIDEO_STATE_PAUSED:
			return "PAUSED";
		case VIDEO_STATE_STOPPING:
			return "STOPPING";
		case VIDEO_STATE_FINISHED:
			return "FINISHED";
		case VIDEO_STATE_ERROR:
			return "ERROR";
		default:
			return "UNKNOWN";
	}
}

static void video_shell_fourcc(char *buffer, u32 fourcc)
{
	u32 index;
	char value;

	if(buffer == NULL)
	{
		return;
	}
	for(index = 0U; index < 4U; index++)
	{
		value = (char)((fourcc >> (index * 8U)) & 0xFFU);
		buffer[index] = (value >= ' ' && value <= '~') ? value : '.';
	}
	buffer[4] = '\0';
}

int video_stats_cmd(int argc, char *argv[])
{
	Video_Context_t *context = s_video;
	Video_Stats_t stats;
	AVI_Info_t info;
	char handler[5];
	char compression[5];
	u32 fps_x1000 = 0U;
	u32 busy = 0U;

	if(argc > 1 && argv[1] != NULL && argv[1][0] == 'r')
	{
		if(context == NULL)
		{
			shellPrintEndLine(&shell, "Video stats unavailable\r\n");
			return -1;
		}
		video_reset_stats(context);
		shellPrintEndLine(&shell, "Video stats reset\r\n");
		return 0;
	}

	if(context == NULL || Video_GetStats(&stats) != SYSTEM_STATUS_OK)
	{
		shellPrintEndLine(&shell, "Video stats unavailable\r\n");
		return -1;
	}
	(void)Video_GetInfo(&info);
	video_shell_fourcc(handler, info.video_handler);
	video_shell_fourcc(compression, info.video_compression);
	if(info.video_scale != 0U)
	{
		fps_x1000 = (info.video_rate / info.video_scale) * 1000U;
		fps_x1000 += (((info.video_rate % info.video_scale) * 1000U) +
		              (info.video_scale / 2U)) / info.video_scale;
	}
	if(context->thread_id != NULL && context->exit_tick == 0U)
	{
		busy = 1U;
	}

	shellPrintEndLine(&shell, "Video stats:\r\n");
	shellPrintEndLine(&shell, "  state=%s result=%s busy=%lu\r\n",
	                  video_shell_state_text(Video_GetState()),
	                  Video_ResultText(Video_GetResult()),
	                  (unsigned long)busy);
	if(context->path[0] != '\0')
	{
		shellPrintEndLine(&shell, "  path=%s\r\n", context->path);
	}
	shellPrintEndLine(&shell, "  video %ux%u frames=%lu fps=%lu.%03lu frame_us=%lu\r\n",
	                  (unsigned int)info.width,
	                  (unsigned int)info.height,
	                  (unsigned long)info.video_length,
	                  (unsigned long)(fps_x1000 / 1000U),
	                  (unsigned long)(fps_x1000 % 1000U),
	                  (unsigned long)info.frame_duration_us);
	shellPrintEndLine(&shell, "  fourcc handler=%s compression=%s scale=%lu rate=%lu\r\n",
	                  handler,
	                  compression,
	                  (unsigned long)info.video_scale,
	                  (unsigned long)info.video_rate);
	shellPrintEndLine(&shell, "  audio has=%u pcm=%u rate=%lu ch=%u bits=%u align=%u avg=%lu\r\n",
	                  (unsigned int)info.has_audio,
	                  (unsigned int)info.audio_supported,
	                  (unsigned long)info.audio_sample_rate,
	                  (unsigned int)info.audio_channels,
	                  (unsigned int)info.audio_bits_per_sample,
	                  (unsigned int)info.audio_block_align,
	                  (unsigned long)info.audio_average_bytes_per_second);
	shellPrintEndLine(&shell, "  chunks video=%lu audio=%lu decoded=%lu displayed=%lu dropped=%lu jpeg_err=%lu\r\n",
	                  (unsigned long)stats.video_chunks,
	                  (unsigned long)stats.audio_chunks,
	                  (unsigned long)stats.video_frames_decoded,
	                  (unsigned long)stats.video_frames_displayed,
	                  (unsigned long)stats.video_frames_dropped,
	                  (unsigned long)stats.jpeg_errors);
	shellPrintEndLine(&shell, "  timing jpeg_max=%lu lcd_wait=%lu lcd_wait_max=%lu lcd_dma_timeout=%lu\r\n",
	                  (unsigned long)stats.max_jpeg_decode_ticks,
	                  (unsigned long)stats.lcd_wait_ticks,
	                  (unsigned long)stats.max_lcd_wait_ticks,
	                  (unsigned long)stats.lcd_dma_timeouts);
	shellPrintEndLine(&shell, "  pcm in=%lu out_frames=%lu fifo=%lu/%lu peak=%lu underrun=%lu\r\n",
	                  (unsigned long)stats.pcm_input_bytes,
	                  (unsigned long)stats.pcm_output_frames,
	                  (unsigned long)stats.audio_fifo_bytes,
	                  (unsigned long)stats.audio_prebuffer_bytes,
	                  (unsigned long)stats.audio_fifo_peak_bytes,
	                  (unsigned long)stats.audio_underruns);
	shellPrintEndLine(&shell, "  queue video=%lu audio=%lu deadline_frames=%lu\r\n",
	                  (unsigned long)stats.pending_video_frames,
	                  (unsigned long)stats.pending_audio_chunks,
	                  (unsigned long)stats.audio_deadline_frames);
	shellPrintEndLine(&shell, "  audio dma=%lu half=%lu full=%lu collide=%lu late=%lu\r\n",
	                  (unsigned long)stats.audio_dma_events,
	                  (unsigned long)stats.audio_dma_half_events,
	                  (unsigned long)stats.audio_dma_full_events,
	                  (unsigned long)stats.audio_dma_flag_collisions,
	                  (unsigned long)stats.audio_late_writes);
	shellPrintEndLine(&shell, "  audio refill=%lu missed=%lu qpeak=%lu media=%lu replay=%lu silence=%lu\r\n",
	                  (unsigned long)stats.audio_refill_events,
	                  (unsigned long)stats.audio_refill_missed,
	                  (unsigned long)stats.audio_refill_queue_peak,
	                  (unsigned long)stats.audio_media_frames,
	                  (unsigned long)stats.audio_replayed_frames,
	                  (unsigned long)stats.audio_silence_frames);
	shellPrintEndLine(&shell, "  audio udr=%lu dma_err=%lu i2s_err=%lu last_err=0x%08lX last_sr=0x%08lX\r\n",
	                  (unsigned long)stats.audio_i2s_underruns,
	                  (unsigned long)stats.audio_dma_errors,
	                  (unsigned long)stats.audio_i2s_errors,
	                  (unsigned long)stats.audio_last_error,
	                  (unsigned long)stats.audio_last_i2s_status);
	shellPrintEndLine(&shell, "  io read_calls=%lu read_bytes=%lu read_err=%lu eoi=%lu\r\n",
	                  (unsigned long)stats.fatfs_read_calls,
	                  (unsigned long)stats.fatfs_read_bytes,
	                  (unsigned long)stats.fatfs_read_errors,
	                  (unsigned long)stats.jpeg_eoi_injected);
	shellPrintEndLine(&shell, "  jpeg_arena_peak=%lu arena_fail=%lu task_stack_free=%lu\r\n",
	                  (unsigned long)stats.jpeg_arena_peak_bytes,
	                  (unsigned long)stats.jpeg_arena_allocation_failures,
	                  (unsigned long)stats.task_stack_space);
	shellPrintEndLine(&shell, "  vmem pool=%lu current=%lu peak=%lu largest=%lu fail=%lu\r\n",
	                  (unsigned long)stats.memory_pool_size,
	                  (unsigned long)stats.memory_current_bytes,
	                  (unsigned long)stats.memory_peak_bytes,
	                  (unsigned long)stats.memory_largest_free_block,
	                  (unsigned long)stats.memory_allocation_failures);
	shellPrintEndLine(&shell, "  mem ccm=%u/1000 ex=%u/1000\r\n",
	                  (unsigned int)sys_mem_perused(SRAMCCM),
	                  (unsigned int)sys_mem_perused(SRAMEX));
	return 0;
}

SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
videostat, video_stats_cmd, video playback diagnostic stats);
