#include "Video.h"

#include "LCD.h"
#include "System_Status.h"
#include "Video_Memory.h"
#include "Video_MJPEG.h"
#include "cmsis_os2.h"
#include "key.h"

#include <string.h>

#define VIDEO_TASK_STACK_SIZE             8192U
#define VIDEO_LAUNCH_DELAY_TICKS          80U
#define VIDEO_CONTROL_POLL_TICKS          5U
#define VIDEO_CLEANUP_DELAY_TICKS         2U
#define VIDEO_KEY_RELEASE_TIMEOUT_TICKS   1000U
#define VIDEO_MAX_CONSECUTIVE_JPEG_ERRORS 8U

#define VIDEO_FLAG_PAUSE                  (1UL << 0)
#define VIDEO_FLAG_RESUME                 (1UL << 1)
#define VIDEO_FLAG_STOP                   (1UL << 2)
#define VIDEO_FLAG_CONTROL_MASK           (VIDEO_FLAG_PAUSE | VIDEO_FLAG_RESUME | VIDEO_FLAG_STOP)

#define VIDEO_CONTROL_NONE                0U
#define VIDEO_CONTROL_PAUSE               1U
#define VIDEO_CONTROL_RESUME              2U
#define VIDEO_CONTROL_STOP                3U

typedef struct Video_Context
{
	FIL file;
	AVI_Parser_t parser;
	Video_MJPEG_t mjpeg;
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
	u8 file_opened;
	u8 mjpeg_opened;
	u8 lcd_owned;
} Video_Context_t;

typedef StaticTask_t osStaticThreadDef_t;

static void video_task(void *argument);
static Video_Context_t *video_get_context(void);
static u8 video_copy_path(char *destination, u32 size, const char *source);
static Video_Result_t video_map_parser_result(int result);
static Video_Result_t video_map_mjpeg_result(Video_MJPEGResult_t result);
static void video_finish(Video_Context_t *context, Video_Result_t result);
static u8 video_wait_launch(Video_Context_t *context);
static u8 video_process_controls(Video_Context_t *context, u32 timeout);
static u8 video_pause_loop(Video_Context_t *context, u32 *deadline);
static u8 video_wait_deadline(Video_Context_t *context, u32 *deadline);
static void video_advance_deadline(Video_Context_t *context, u32 *deadline);
static u8 video_prepare_clock(Video_Context_t *context);
static u8 video_clear_display(Video_Context_t *context);
static void video_wait_key_release(void);
static void video_update_max(u32 *maximum, u32 value);
static void video_update_memory_stats(Video_Context_t *context);

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
		s_video_thread_attr.stack_mem = Video_Memory_Alloc(
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
		Video_Memory_Free(s_video_thread_attr.stack_mem);
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
		Video_Memory_Free(s_video_thread_attr.stack_mem);
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
	*info = s_video->info;
	return SYSTEM_STATUS_OK;
}

int Video_GetStats(Video_Stats_t *stats)
{
	if(s_video == NULL || stats == NULL)
	{
		return OPERATE_FAILED;
	}
	video_update_memory_stats(s_video);
	*stats = s_video->stats;
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
			return "AVI audio is not ready";
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
	AVI_Chunk_t chunk;
	Video_MJPEGResult_t decode_result;
	Video_Result_t final_result = VIDEO_RESULT_EOF;
	u32 deadline;
	u32 now;
	u32 decode_start;
	u32 consecutive_jpeg_errors = 0U;
	u32 drop_threshold;
	u8 control;
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
	context->info = context->parser.info;
	if(context->info.has_audio != 0U)
	{
		video_finish(context, VIDEO_RESULT_UNSUPPORTED_AUDIO);
		return;
	}
	if(video_prepare_clock(context) == 0U)
	{
		video_finish(context, VIDEO_RESULT_UNSUPPORTED);
		return;
	}

	context->state = VIDEO_STATE_PREBUFFER;
	if(Video_MJPEG_Init(&context->mjpeg) != 0)
	{
		video_finish(context, VIDEO_RESULT_NO_MEMORY);
		return;
	}
	context->mjpeg_opened = 1U;
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

	deadline = osKernelGetTickCount();
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
			final_result = VIDEO_RESULT_USER_STOP;
			break;
		}
		if(context->state == VIDEO_STATE_PAUSED ||
		   control == VIDEO_CONTROL_PAUSE)
		{
			if(video_pause_loop(context, &deadline) == VIDEO_CONTROL_STOP)
			{
				final_result = VIDEO_RESULT_USER_STOP;
				break;
			}
		}

		parser_result = AVI_Parser_Next(&context->parser, &chunk);
		if(parser_result == AVI_PARSER_EOF)
		{
			final_result = VIDEO_RESULT_EOF;
			break;
		}
		if(parser_result != AVI_PARSER_OK)
		{
			final_result = video_map_parser_result(parser_result);
			break;
		}
		if(chunk.type == AVI_CHUNK_AUDIO)
		{
			context->stats.audio_chunks++;
			continue;
		}
		if(chunk.type != AVI_CHUNK_VIDEO)
		{
			continue;
		}
		context->stats.video_chunks++;

		if(video_wait_deadline(context, &deadline) == VIDEO_CONTROL_STOP)
		{
			final_result = VIDEO_RESULT_USER_STOP;
			break;
		}
		now = osKernelGetTickCount();
		if((s32)(now - deadline) > (s32)drop_threshold)
		{
			context->stats.video_frames_dropped++;
			video_advance_deadline(context, &deadline);
			continue;
		}

		decode_start = osKernelGetTickCount();
		decode_result = Video_MJPEG_DecodeFrame(&context->mjpeg, &context->file,
			&chunk, context->info.width, context->info.height);
		video_update_max(&context->stats.max_jpeg_decode_ticks,
		                 osKernelGetTickCount() - decode_start);
		video_advance_deadline(context, &deadline);
		if(decode_result == VIDEO_MJPEG_OK)
		{
			context->stats.video_frames_decoded++;
			context->stats.video_frames_displayed++;
			consecutive_jpeg_errors = 0U;
			continue;
		}

		context->stats.jpeg_errors++;
		if(decode_result == VIDEO_MJPEG_FORMAT_ERROR &&
		   consecutive_jpeg_errors < VIDEO_MAX_CONSECUTIVE_JPEG_ERRORS)
		{
			consecutive_jpeg_errors++;
			continue;
		}
		final_result = video_map_mjpeg_result(decode_result);
		break;
	}

	video_finish(context, final_result);
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
	if(context->mjpeg_opened != 0U)
	{
		context->stats.jpeg_eoi_injected = context->mjpeg.jpeg_eoi_injected;
		context->stats.fatfs_read_errors += context->mjpeg.fatfs_read_errors;
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

	flags = osThreadFlagsWait(VIDEO_FLAG_CONTROL_MASK, osFlagsWaitAny, timeout);
	if((flags & osFlagsError) != 0U)
	{
		flags = 0U;
	}
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
	u8 control;

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
			*deadline += osKernelGetTickCount() - pause_start;
			context->state = VIDEO_STATE_PLAYING;
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
