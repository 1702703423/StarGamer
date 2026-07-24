#include "Video_PCM.h"

#include "Malloc.h"
#include "i2s.h"

#include <string.h>

#define VIDEO_PCM_PACKAGE_MS        24U
#define VIDEO_PCM_MIN_PACKAGE_SIZE  512U
#define VIDEO_PCM_FEED_QUEUE_DEPTH  8U
#define VIDEO_PCM_FEED_STACK_SIZE   2048U
#define VIDEO_PCM_FEED_STOP_INDEX   0xFFU

typedef struct Video_PCMFeedEvent
{
	u8 index;
	u8 reserved[3];
	u32 sequence;
} Video_PCMFeedEvent_t;

static void video_pcm_dma_callback(u8 idle_index, u32 sequence);
static void video_pcm_feed_task(void *argument);
static u32 video_pcm_choose_package_size(const AVI_Info_t *info);
static int video_pcm_fill_dma(Video_PCM_t *pcm, u8 index);
static int video_pcm_start_feeder(Video_PCM_t *pcm);
static void video_pcm_stop_feeder(Video_PCM_t *pcm);
static u32 video_pcm_align_water(u32 value, u32 block_align);
static u32 video_pcm_length(const Video_PCM_t *pcm);

static Video_PCM_t *s_video_pcm_active;
static StaticTask_t s_video_audio_feed_cb;
static osThreadAttr_t s_video_audio_feed_attr =
{
	.name = "VideoAudioFeedTask",
	.cb_mem = &s_video_audio_feed_cb,
	.cb_size = sizeof(s_video_audio_feed_cb),
	.stack_mem = NULL,
	.stack_size = VIDEO_PCM_FEED_STACK_SIZE,
	.priority = (osPriority_t)osPriorityHigh,
};
static StaticQueue_t s_video_audio_feed_queue_cb;
static u32 s_video_audio_feed_queue_mem[
	(VIDEO_PCM_FEED_QUEUE_DEPTH * sizeof(Video_PCMFeedEvent_t) + 3U) / 4U];
static const osMessageQueueAttr_t s_video_audio_feed_queue_attr =
{
	.name = "VideoAudioFeedQueue",
	.cb_mem = &s_video_audio_feed_queue_cb,
	.cb_size = sizeof(s_video_audio_feed_queue_cb),
	.mq_mem = &s_video_audio_feed_queue_mem[0],
	.mq_size = sizeof(s_video_audio_feed_queue_mem),
};

int Video_PCM_Open(Video_PCM_t *pcm, const AVI_Info_t *info)
{
	u32 package_size;

	if(pcm == NULL || info == NULL ||
	   info->audio_supported == 0U ||
	   info->audio_format_tag != AVI_AUDIO_FORMAT_PCM ||
	   info->audio_block_align == 0U || s_video_pcm_active != NULL)
	{
		return VIDEO_PCM_FORMAT_ERROR;
	}

	memset(pcm, 0, sizeof(*pcm));
	package_size = video_pcm_choose_package_size(info);
	if(package_size == 0U)
	{
		return VIDEO_PCM_FORMAT_ERROR;
	}

	pcm->fifo_storage = (u8 *)sys_malloc(SRAMEX, VIDEO_PCM_FIFO_SIZE);
	pcm->io_buffer = (u8 *)sys_malloc(SRAMEX, VIDEO_PCM_IO_BUFFER_SIZE);
	pcm->feed_buffer = (u8 *)sys_malloc(SRAMEX, package_size);
	if(pcm->fifo_storage == NULL || pcm->io_buffer == NULL ||
	   pcm->feed_buffer == NULL)
	{
		if(pcm->feed_buffer != NULL)
		{
			sys_free(SRAMEX, pcm->feed_buffer);
		}
		if(pcm->io_buffer != NULL)
		{
			sys_free(SRAMEX, pcm->io_buffer);
		}
		if(pcm->fifo_storage != NULL)
		{
			sys_free(SRAMEX, pcm->fifo_storage);
		}
		memset(pcm, 0, sizeof(*pcm));
		return VIDEO_PCM_MEMORY_ERROR;
	}
	if(__kfifo_init(&pcm->fifo, pcm->fifo_storage,
	                VIDEO_PCM_FIFO_SIZE, sizeof(u8)) != 0)
	{
		sys_free(SRAMEX, pcm->feed_buffer);
		sys_free(SRAMEX, pcm->io_buffer);
		sys_free(SRAMEX, pcm->fifo_storage);
		memset(pcm, 0, sizeof(*pcm));
		return VIDEO_PCM_MEMORY_ERROR;
	}

	pcm->sample_rate = info->audio_sample_rate;
	pcm->block_align = info->audio_block_align;
	pcm->package_size = package_size;
	pcm->package_frames = package_size / pcm->block_align;
	pcm->prebuffer_bytes = video_pcm_align_water(VIDEO_PCM_PREBUFFER_BYTES,
	                                             pcm->block_align);
	pcm->low_water_bytes = video_pcm_align_water(VIDEO_PCM_LOW_WATER_BYTES,
	                                            pcm->block_align);
	pcm->high_water_bytes = video_pcm_align_water(VIDEO_PCM_HIGH_WATER_BYTES,
	                                             pcm->block_align);
	if(pcm->prebuffer_bytes > (VIDEO_PCM_FIFO_SIZE - package_size))
	{
		pcm->prebuffer_bytes = VIDEO_PCM_FIFO_SIZE - package_size;
		pcm->prebuffer_bytes -= pcm->prebuffer_bytes % pcm->block_align;
	}
	if(pcm->high_water_bytes > (VIDEO_PCM_FIFO_SIZE - package_size))
	{
		pcm->high_water_bytes = VIDEO_PCM_FIFO_SIZE - package_size;
		pcm->high_water_bytes -= pcm->high_water_bytes % pcm->block_align;
	}
	if(pcm->low_water_bytes > pcm->prebuffer_bytes)
	{
		pcm->low_water_bytes = pcm->prebuffer_bytes;
	}
	pcm->dma_timeout_ticks =
		((pcm->package_frames * osKernelGetTickFreq() + pcm->sample_rate - 1U) /
		 pcm->sample_rate) * 4U + 10U;

	if(video_pcm_start_feeder(pcm) != VIDEO_PCM_OK)
	{
		Video_PCM_Close(pcm);
		return VIDEO_PCM_MEMORY_ERROR;
	}

	(void)audio_card_init(audio_card_get_volume());
	if(audio_card_open_prepare((int)info->audio_sample_rate,
	                           (int)info->audio_bits_per_sample,
	                           (int)info->audio_channels,
	                           (int)package_size) != 0)
	{
		Video_PCM_Close(pcm);
		return VIDEO_PCM_AUDIO_ERROR;
	}

	pcm->opened = 1U;
	s_video_pcm_active = pcm;
	__DMB();
	audio_card_register_tx_index(video_pcm_dma_callback);
	return VIDEO_PCM_OK;
}

void Video_PCM_Close(Video_PCM_t *pcm)
{
	if(pcm == NULL)
	{
		return;
	}

	pcm->opened = 0U;
	pcm->started = 0U;
	__DMB();
	audio_card_register_tx_index(NULL);
	video_pcm_stop_feeder(pcm);
	if(s_video_pcm_active == pcm)
	{
		s_video_pcm_active = NULL;
	}
	(void)audio_card_close();

	if(pcm->io_buffer != NULL)
	{
		sys_free(SRAMEX, pcm->io_buffer);
	}
	if(pcm->feed_buffer != NULL)
	{
		sys_free(SRAMEX, pcm->feed_buffer);
	}
	if(pcm->fifo_storage != NULL)
	{
		sys_free(SRAMEX, pcm->fifo_storage);
	}
	memset(pcm, 0, sizeof(*pcm));
}

int Video_PCM_ReadChunkPart(Video_PCM_t *pcm, FIL *file,
	u32 *offset, u32 *remaining)
{
	FRESULT fatfs_result = FR_OK;
	UINT bytes_read = 0U;
	u32 request_size;
	u32 free_bytes;
	u32 buffered_bytes;
	u32 fatfs_operation = VIDEO_PCM_FATFS_OP_NONE;

	if(pcm == NULL || file == NULL || offset == NULL || remaining == NULL ||
	   pcm->opened == 0U || pcm->block_align == 0U)
	{
		return VIDEO_PCM_ERROR;
	}
	if(*remaining == 0U)
	{
		return VIDEO_PCM_OK;
	}
	if((*remaining % pcm->block_align) != 0U)
	{
		return VIDEO_PCM_FORMAT_ERROR;
	}

	free_bytes = Video_PCM_GetFreeBytes(pcm);
	if(free_bytes < pcm->block_align)
	{
		return VIDEO_PCM_WOULD_BLOCK;
	}
	request_size = *remaining;
	if(request_size > VIDEO_PCM_IO_BUFFER_SIZE)
	{
		request_size = VIDEO_PCM_IO_BUFFER_SIZE;
	}
	if(request_size > free_bytes)
	{
		request_size = free_bytes;
	}
	request_size -= request_size % pcm->block_align;
	if(request_size == 0U)
	{
		return VIDEO_PCM_WOULD_BLOCK;
	}

	pcm->fatfs_read_calls++;
	if((u32)f_tell(file) != *offset)
	{
		AVI_IOTrace_Begin(&pcm->fatfs_io_trace, AVI_IO_OP_SEEK,
		                  *offset, 0U, osKernelGetTickCount());
		fatfs_result = f_lseek(file, (FSIZE_t)*offset);
		AVI_IOTrace_End(&pcm->fatfs_io_trace, osKernelGetTickCount());
		if(fatfs_result != FR_OK)
		{
			fatfs_operation = VIDEO_PCM_FATFS_OP_SEEK;
		}
	}
	if(fatfs_operation == VIDEO_PCM_FATFS_OP_NONE)
	{
		AVI_IOTrace_Begin(&pcm->fatfs_io_trace, AVI_IO_OP_READ,
		                  *offset, request_size, osKernelGetTickCount());
		fatfs_result = f_read(file, pcm->io_buffer, (UINT)request_size,
		                       &bytes_read);
		AVI_IOTrace_End(&pcm->fatfs_io_trace, osKernelGetTickCount());
		if(fatfs_result != FR_OK)
		{
			fatfs_operation = VIDEO_PCM_FATFS_OP_READ;
		}
		else if(bytes_read != (UINT)request_size)
		{
			fatfs_operation = VIDEO_PCM_FATFS_OP_SHORT_READ;
		}
	}
	if(fatfs_operation != VIDEO_PCM_FATFS_OP_NONE)
	{
		pcm->fatfs_read_errors++;
		pcm->fatfs_last_operation = fatfs_operation;
		pcm->fatfs_last_offset = *offset;
		pcm->fatfs_last_request_bytes = request_size;
		pcm->fatfs_last_read_bytes = (u32)bytes_read;
		pcm->fatfs_last_result = (s32)fatfs_result;
		return VIDEO_PCM_IO_ERROR;
	}
	pcm->fatfs_read_bytes += request_size;
	if(__kfifo_in(&pcm->fifo, pcm->io_buffer, request_size) != request_size)
	{
		return VIDEO_PCM_ERROR;
	}

	*offset += request_size;
	*remaining -= request_size;
	pcm->input_bytes += request_size;
	buffered_bytes = video_pcm_length(pcm);
	if(buffered_bytes > pcm->fifo_peak_bytes)
	{
		pcm->fifo_peak_bytes = buffered_bytes;
	}
	return VIDEO_PCM_OK;
}

int Video_PCM_Start(Video_PCM_t *pcm)
{
	u32 buffered_bytes;

	if(pcm == NULL || pcm->opened == 0U)
	{
		return VIDEO_PCM_ERROR;
	}
	if(pcm->started != 0U)
	{
		return VIDEO_PCM_OK;
	}

	buffered_bytes = video_pcm_length(pcm);
	if(buffered_bytes == 0U ||
	   (pcm->input_eof == 0U && buffered_bytes < (pcm->package_size * 2U)))
	{
		return VIDEO_PCM_WOULD_BLOCK;
	}
	if(video_pcm_fill_dma(pcm, 0U) != VIDEO_PCM_OK ||
	   video_pcm_fill_dma(pcm, 1U) != VIDEO_PCM_OK)
	{
		return VIDEO_PCM_AUDIO_ERROR;
	}

	pcm->started = 1U;
	pcm->last_dma_tick = osKernelGetTickCount();
	__DMB();
	if(audio_card_start() != 0)
	{
		pcm->started = 0U;
		return VIDEO_PCM_AUDIO_ERROR;
	}
	return VIDEO_PCM_OK;
}

int Video_PCM_CheckHealth(const Video_PCM_t *pcm)
{
	audio_card_stats_t stats;
	u32 now;
	u32 last_dma_tick;

	if(pcm == NULL || pcm->opened == 0U)
	{
		return VIDEO_PCM_ERROR;
	}
	audio_card_get_stats(&stats);
	if(stats.dma_error_count != 0U ||
	   (stats.last_error & ~HAL_I2S_ERROR_UDR) != 0U)
	{
		return VIDEO_PCM_AUDIO_ERROR;
	}
	if(pcm->feed_error != 0U)
	{
		return VIDEO_PCM_AUDIO_ERROR;
	}
	if(pcm->started == 0U || pcm->paused != 0U)
	{
		return VIDEO_PCM_OK;
	}

	__DMB();
	last_dma_tick = pcm->last_dma_tick;
	__DMB();
	now = osKernelGetTickCount();
	return ((now - last_dma_tick) <= pcm->dma_timeout_ticks) ?
	       VIDEO_PCM_OK : VIDEO_PCM_AUDIO_ERROR;
}

int Video_PCM_Pause(Video_PCM_t *pcm)
{
	if(pcm == NULL || pcm->started == 0U || pcm->paused != 0U)
	{
		return VIDEO_PCM_ERROR;
	}
	if(audio_card_pause() != 0)
	{
		return VIDEO_PCM_AUDIO_ERROR;
	}
	pcm->paused = 1U;
	return VIDEO_PCM_OK;
}

int Video_PCM_Resume(Video_PCM_t *pcm)
{
	if(pcm == NULL || pcm->started == 0U || pcm->paused == 0U)
	{
		return VIDEO_PCM_ERROR;
	}
	if(audio_card_resume() != 0)
	{
		return VIDEO_PCM_AUDIO_ERROR;
	}
	pcm->last_dma_tick = osKernelGetTickCount();
	pcm->paused = 0U;
	return VIDEO_PCM_OK;
}

void Video_PCM_SetInputEof(Video_PCM_t *pcm)
{
	if(pcm != NULL)
	{
		pcm->input_eof = 1U;
	}
}

u32 Video_PCM_GetBufferedBytes(const Video_PCM_t *pcm)
{
	return video_pcm_length(pcm);
}

u32 Video_PCM_GetFreeBytes(const Video_PCM_t *pcm)
{
	u32 length;

	if(pcm == NULL || pcm->fifo.mask == 0U)
	{
		return 0U;
	}
	length = video_pcm_length(pcm);
	return (pcm->fifo.mask + 1U) - length;
}

u32 Video_PCM_GetPlayedFrames(const Video_PCM_t *pcm)
{
	u32 frames;

	if(pcm == NULL)
	{
		return 0U;
	}
	__DMB();
	frames = pcm->played_frames;
	__DMB();
	return frames;
}

u8 Video_PCM_IsDrained(const Video_PCM_t *pcm)
{
	if(pcm == NULL || pcm->input_eof == 0U || video_pcm_length(pcm) != 0U)
	{
		return 0U;
	}
	__DMB();
	return (pcm->dma_valid_bytes[0] == 0U &&
	        pcm->dma_valid_bytes[1] == 0U) ? 1U : 0U;
}

void Video_PCM_GetStats(const Video_PCM_t *pcm, Video_PCMStats_t *stats)
{
	audio_card_stats_t card_stats;

	if(pcm == NULL || stats == NULL)
	{
		return;
	}

	audio_card_get_stats(&card_stats);
	stats->input_bytes = pcm->input_bytes;
	stats->output_frames = Video_PCM_GetPlayedFrames(pcm);
	stats->fifo_peak_bytes = pcm->fifo_peak_bytes;
	stats->underruns = pcm->underruns;
	stats->dma_events = pcm->dma_events;
	stats->dma_flag_collisions = pcm->dma_flag_collisions;
	stats->refill_events = pcm->refill_events;
	stats->refill_missed = pcm->refill_missed;
	stats->refill_queue_peak = pcm->refill_queue_peak;
	stats->media_frames = pcm->media_frames;
	stats->replayed_frames = pcm->replayed_frames;
	stats->silence_frames = pcm->silence_frames;
	stats->dma_half_events = card_stats.half_count;
	stats->dma_full_events = card_stats.full_count;
	stats->dma_errors = card_stats.dma_error_count;
	stats->i2s_errors = card_stats.i2s_error_count;
	stats->i2s_underruns = card_stats.udr_count;
	stats->late_writes = card_stats.late_write_count;
	stats->last_audio_error = card_stats.last_error;
	stats->last_i2s_status = card_stats.last_sr;
	stats->fatfs_read_errors = pcm->fatfs_read_errors;
	stats->fatfs_read_calls = pcm->fatfs_read_calls;
	stats->fatfs_read_bytes = pcm->fatfs_read_bytes;
	stats->fatfs_last_operation = pcm->fatfs_last_operation;
	stats->fatfs_last_offset = pcm->fatfs_last_offset;
	stats->fatfs_last_request_bytes = pcm->fatfs_last_request_bytes;
	stats->fatfs_last_read_bytes = pcm->fatfs_last_read_bytes;
	stats->fatfs_last_result = pcm->fatfs_last_result;
	__DMB();
	stats->fatfs_io_active_operation =
		pcm->fatfs_io_trace.active_operation;
	__DMB();
	stats->fatfs_io_active_offset = pcm->fatfs_io_trace.active_offset;
	stats->fatfs_io_active_request_bytes =
		pcm->fatfs_io_trace.active_request_bytes;
	stats->fatfs_io_active_start_tick =
		pcm->fatfs_io_trace.active_start_tick;
	stats->fatfs_io_last_ticks = pcm->fatfs_io_trace.last_ticks;
	stats->fatfs_io_max_ticks = pcm->fatfs_io_trace.max_ticks;
	stats->fatfs_io_slow_count = pcm->fatfs_io_trace.slow_count;
	stats->last_dma_tick = pcm->last_dma_tick;
	__DMB();
}

static void video_pcm_dma_callback(u8 idle_index, u32 sequence)
{
	Video_PCM_t *pcm = s_video_pcm_active;
	Video_PCMFeedEvent_t event;
	u32 valid_bytes;
	u32 valid_frames;
	u32 queued;
	u32 generation;
	u32 silent_frames;

	if(pcm == NULL || pcm->opened == 0U || pcm->started == 0U ||
	   pcm->feed_queue == NULL)
	{
		return;
	}
	if(idle_index > 1U)
	{
		return;
	}

	__DMB();
	generation = pcm->dma_generation[idle_index];
	valid_bytes = pcm->dma_valid_bytes[idle_index];
	__DMB();
	if(generation == pcm->dma_played_generation[idle_index])
	{
		pcm->replayed_frames += pcm->package_frames;
	}
	else
	{
		pcm->dma_played_generation[idle_index] = generation;
		valid_frames = valid_bytes / pcm->block_align;
		if(valid_frames > pcm->package_frames)
		{
			valid_frames = pcm->package_frames;
		}
		pcm->media_frames += valid_frames;
		silent_frames = pcm->package_frames - valid_frames;
		if(silent_frames != 0U)
		{
			pcm->silence_frames += silent_frames;
		}
	}
	pcm->dma_valid_bytes[idle_index] = 0U;
	pcm->played_frames += pcm->package_frames;
	pcm->dma_events++;
	pcm->last_dma_tick = osKernelGetTickCount();

	event.index = idle_index;
	event.reserved[0] = 0U;
	event.reserved[1] = 0U;
	event.reserved[2] = 0U;
	event.sequence = sequence;
	if(osMessageQueuePut(pcm->feed_queue, &event, 0U, 0U) != osOK)
	{
		pcm->refill_missed++;
		return;
	}
	pcm->refill_events++;
	queued = osMessageQueueGetCount(pcm->feed_queue);
	if(queued > pcm->refill_queue_peak)
	{
		pcm->refill_queue_peak = queued;
	}
}

static void video_pcm_feed_task(void *argument)
{
	Video_PCM_t *pcm = (Video_PCM_t *)argument;
	Video_PCMFeedEvent_t event;
	u32 missed;

	if(pcm == NULL)
	{
		osThreadExit();
		return;
	}
	pcm->feed_exited = 0U;
	__DMB();

	while(1)
	{
		if(osMessageQueueGet(pcm->feed_queue, &event, NULL, osWaitForever) != osOK)
		{
			pcm->feed_error = 1U;
			break;
		}
		if(pcm->feed_stop != 0U || event.index == VIDEO_PCM_FEED_STOP_INDEX)
		{
			break;
		}
		if(event.index > 1U)
		{
			pcm->refill_missed++;
			continue;
		}
		if(pcm->feed_last_sequence != 0U &&
		   event.sequence > (pcm->feed_last_sequence + 1U))
		{
			missed = event.sequence - pcm->feed_last_sequence - 1U;
			pcm->refill_missed += missed;
		}
		pcm->feed_last_sequence = event.sequence;

		if(video_pcm_fill_dma(pcm, event.index) != VIDEO_PCM_OK)
		{
			pcm->feed_error = 1U;
			break;
		}
	}

	pcm->feed_exited = 1U;
	__DMB();
	osThreadExit();
}

static u32 video_pcm_choose_package_size(const AVI_Info_t *info)
{
	u32 package_size;
	u32 minimum_size;
	u32 maximum_size;
	u32 block_align;

	if(info == NULL || info->audio_block_align == 0U ||
	   info->audio_average_bytes_per_second == 0U)
	{
		return 0U;
	}
	block_align = info->audio_block_align;
	minimum_size = ((VIDEO_PCM_MIN_PACKAGE_SIZE + block_align - 1U) /
	                block_align) * block_align;
	maximum_size = VIDEO_PCM_IO_BUFFER_SIZE -
	               (VIDEO_PCM_IO_BUFFER_SIZE % block_align);
	if(minimum_size > maximum_size)
	{
		return 0U;
	}

	package_size = (info->audio_average_bytes_per_second *
	                VIDEO_PCM_PACKAGE_MS) / 1000U;
	package_size -= package_size % block_align;
	if(package_size < minimum_size)
	{
		package_size = minimum_size;
	}
	if(package_size > maximum_size)
	{
		package_size = maximum_size;
	}
	return package_size;
}

static int video_pcm_start_feeder(Video_PCM_t *pcm)
{
	if(pcm == NULL)
	{
		return VIDEO_PCM_ERROR;
	}
	if(pcm->feed_queue == NULL)
	{
		pcm->feed_queue = osMessageQueueNew(VIDEO_PCM_FEED_QUEUE_DEPTH,
			sizeof(Video_PCMFeedEvent_t), &s_video_audio_feed_queue_attr);
		if(pcm->feed_queue == NULL)
		{
			return VIDEO_PCM_MEMORY_ERROR;
		}
	}
	if(s_video_audio_feed_attr.stack_mem == NULL)
	{
		s_video_audio_feed_attr.stack_mem =
			sys_thread_stack_alloc((u32)s_video_audio_feed_attr.stack_size);
	}
	if(s_video_audio_feed_attr.stack_mem == NULL)
	{
		(void)osMessageQueueDelete(pcm->feed_queue);
		pcm->feed_queue = NULL;
		return VIDEO_PCM_MEMORY_ERROR;
	}

	pcm->feed_stop = 0U;
	pcm->feed_exited = 0U;
	pcm->feed_error = 0U;
	pcm->feed_last_sequence = 0U;
	pcm->feed_thread = osThreadNew(video_pcm_feed_task, pcm,
		&s_video_audio_feed_attr);
	if(pcm->feed_thread == NULL)
	{
		(void)osMessageQueueDelete(pcm->feed_queue);
		pcm->feed_queue = NULL;
		if(s_video_audio_feed_attr.stack_mem != NULL)
		{
			sys_thread_stack_free(s_video_audio_feed_attr.stack_mem);
			s_video_audio_feed_attr.stack_mem = NULL;
		}
		return VIDEO_PCM_MEMORY_ERROR;
	}
	return VIDEO_PCM_OK;
}

static void video_pcm_stop_feeder(Video_PCM_t *pcm)
{
	Video_PCMFeedEvent_t event;

	if(pcm == NULL)
	{
		return;
	}
	pcm->feed_stop = 1U;
	__DMB();
	if(pcm->feed_queue != NULL)
	{
		event.index = VIDEO_PCM_FEED_STOP_INDEX;
		event.reserved[0] = 0U;
		event.reserved[1] = 0U;
		event.reserved[2] = 0U;
		event.sequence = 0U;
		(void)osMessageQueuePut(pcm->feed_queue, &event, 0U, 0U);
	}
	while(pcm->feed_thread != NULL && pcm->feed_exited == 0U)
	{
		(void)osDelay(1U);
	}
	if(pcm->feed_thread != NULL && pcm->feed_exited != 0U)
	{
		(void)osDelay(2U);
		pcm->feed_thread = NULL;
		if(s_video_audio_feed_attr.stack_mem != NULL)
		{
			sys_thread_stack_free(s_video_audio_feed_attr.stack_mem);
			s_video_audio_feed_attr.stack_mem = NULL;
		}
	}
	if(pcm->feed_queue != NULL)
	{
		(void)osMessageQueueDelete(pcm->feed_queue);
		pcm->feed_queue = NULL;
	}
}

static u32 video_pcm_align_water(u32 value, u32 block_align)
{
	if(block_align == 0U)
	{
		return 0U;
	}
	value -= value % block_align;
	return value;
}

static int video_pcm_fill_dma(Video_PCM_t *pcm, u8 index)
{
	u32 read_size;

	if(pcm == NULL || pcm->feed_buffer == NULL || index > 1U)
	{
		return VIDEO_PCM_ERROR;
	}
	read_size = video_pcm_length(pcm);
	if(read_size > pcm->package_size)
	{
		read_size = pcm->package_size;
	}
	read_size -= read_size % pcm->block_align;
	if(read_size != 0U &&
	   __kfifo_out(&pcm->fifo, pcm->feed_buffer, read_size) != read_size)
	{
		return VIDEO_PCM_ERROR;
	}
	if(read_size < pcm->package_size && pcm->input_eof == 0U)
	{
		pcm->underruns++;
	}
	if(audio_card_write_to_buffer(index, pcm->feed_buffer, (int)read_size) != 0)
	{
		return VIDEO_PCM_AUDIO_ERROR;
	}
	pcm->dma_valid_bytes[index] = read_size;
	pcm->dma_generation[index]++;
	__DMB();
	return VIDEO_PCM_OK;
}

static u32 video_pcm_length(const Video_PCM_t *pcm)
{
	if(pcm == NULL || pcm->fifo.mask == 0U)
	{
		return 0U;
	}
	return pcm->fifo.in - pcm->fifo.out;
}
