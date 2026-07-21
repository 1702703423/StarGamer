#include "AVI_Parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

FRESULT f_lseek(FIL *file, unsigned long offset)
{
	if(file == NULL || file->handle == NULL || offset > file->size)
	{
		return FR_DISK_ERR;
	}
	return (fseek(file->handle, (long)offset, SEEK_SET) == 0) ? FR_OK : FR_DISK_ERR;
}

FRESULT f_read(FIL *file, void *buffer, UINT size, UINT *bytes_read)
{
	size_t count;

	if(file == NULL || file->handle == NULL || buffer == NULL || bytes_read == NULL)
	{
		return FR_DISK_ERR;
	}
	count = fread(buffer, 1U, size, file->handle);
	*bytes_read = (UINT)count;
	return ferror(file->handle) ? FR_DISK_ERR : FR_OK;
}

static int open_host_file(const char *path, FIL *file)
{
	long size;

	memset(file, 0, sizeof(*file));
	file->handle = fopen(path, "rb");
	if(file->handle == NULL)
	{
		return 0;
	}
	if(fseek(file->handle, 0L, SEEK_END) != 0)
	{
		fclose(file->handle);
		return 0;
	}
	size = ftell(file->handle);
	if(size < 0 || (unsigned long)size > 0xFFFFFFFFUL)
	{
		fclose(file->handle);
		return 0;
	}
	file->size = (unsigned long)size;
	return (fseek(file->handle, 0L, SEEK_SET) == 0) ? 1 : 0;
}

int main(int argc, char **argv)
{
	FIL file;
	AVI_Parser_t parser;
	AVI_Chunk_t chunk;
	const AVI_Info_t *info;
	u32 video_chunks = 0U;
	u32 audio_chunks = 0U;
	u32 unknown_chunks = 0U;
	u32 first_video_header = 0U;
	u32 audio_bytes = 0U;
	u32 window_video_chunks = 0U;
	u32 window_audio_chunks = 0U;
	u32 window_audio_bytes = 0U;
	u32 window_audio_min = 0xFFFFFFFFUL;
	u32 window_audio_max = 0U;
	u32 window_audio_misaligned = 0U;
	u32 window_bad_soi = 0U;
	u32 window_bad_eoi = 0U;
	u32 window_start_us = 0U;
	u32 window_end_us = 0U;
	u8 marker[2];
	u8 trace_window = 0U;
	UINT marker_read = 0U;
	int result;

	if(argc != 2 && argc != 4)
	{
		fprintf(stderr, "usage: avi_parser_host <file.avi> [start_sec end_sec]\n");
		return 2;
	}
	if(argc == 4)
	{
		window_start_us = (u32)strtoul(argv[2], NULL, 10) * 1000000UL;
		window_end_us = (u32)strtoul(argv[3], NULL, 10) * 1000000UL;
		if(window_end_us <= window_start_us)
		{
			fprintf(stderr, "invalid time window\n");
			return 2;
		}
		trace_window = 1U;
	}
	if(open_host_file(argv[1], &file) == 0)
	{
		fprintf(stderr, "open failed: %s\n", argv[1]);
		return 2;
	}

	result = AVI_Parser_Open(&parser, &file);
	if(result != AVI_PARSER_OK)
	{
		fprintf(stderr, "AVI_Parser_Open=%d\n", result);
		fclose(file.handle);
		return 1;
	}
	info = AVI_Parser_GetInfo(&parser);
	printf("video=%ux%u stream=%u scale=%lu rate=%lu frame_us=%lu\n",
	       (unsigned int)info->width,
	       (unsigned int)info->height,
	       (unsigned int)info->video_stream_index,
	       (unsigned long)info->video_scale,
	       (unsigned long)info->video_rate,
	       (unsigned long)info->frame_duration_us);
	printf("audio=%u supported=%u stream=%u tag=0x%04X rate=%lu channels=%u\n",
	       (unsigned int)info->has_audio,
	       (unsigned int)info->audio_supported,
	       (unsigned int)info->audio_stream_index,
	       (unsigned int)info->audio_format_tag,
	       (unsigned long)info->audio_sample_rate,
	       (unsigned int)info->audio_channels);

	while((result = AVI_Parser_Next(&parser, &chunk)) == AVI_PARSER_OK)
	{
		u32 chunk_time_us = 0U;
		u8 in_window = 0U;

		if(chunk.data_offset > parser.file_size ||
		   chunk.data_size > (parser.file_size - chunk.data_offset))
		{
			fprintf(stderr, "chunk escaped file boundary\n");
			fclose(file.handle);
			return 1;
		}
		if(chunk.type == AVI_CHUNK_VIDEO)
		{
			chunk_time_us = video_chunks * info->frame_duration_us;
			in_window = (trace_window != 0U &&
			             chunk_time_us >= window_start_us &&
			             chunk_time_us < window_end_us) ? 1U : 0U;
			if(video_chunks == 0U)
			{
				first_video_header = chunk.data_offset - 8U;
			}
			if(in_window != 0U)
			{
				window_video_chunks++;
				if(chunk.data_size < 2U ||
				   f_lseek(&file, chunk.data_offset) != FR_OK ||
				   f_read(&file, marker, sizeof(marker), &marker_read) != FR_OK ||
				   marker_read != (UINT)sizeof(marker) || marker[0] != 0xFFU ||
				   marker[1] != 0xD8U)
				{
					window_bad_soi++;
				}
				if(chunk.data_size < 2U ||
				   f_lseek(&file, chunk.data_offset + chunk.data_size - 2U) != FR_OK ||
				   f_read(&file, marker, sizeof(marker), &marker_read) != FR_OK ||
				   marker_read != (UINT)sizeof(marker) || marker[0] != 0xFFU ||
				   marker[1] != 0xD9U)
				{
					window_bad_eoi++;
				}
				printf("V frame=%lu time_ms=%lu off=0x%08lX size=%lu\n",
				       (unsigned long)video_chunks,
				       (unsigned long)(chunk_time_us / 1000U),
				       (unsigned long)(chunk.data_offset - 8U),
				       (unsigned long)chunk.data_size);
			}
			video_chunks++;
		}
		else if(chunk.type == AVI_CHUNK_AUDIO)
		{
			if(info->audio_average_bytes_per_second != 0U)
			{
				chunk_time_us = (u32)(((unsigned long long)audio_bytes * 1000000ULL) /
				                      info->audio_average_bytes_per_second);
			}
			in_window = (trace_window != 0U &&
			             chunk_time_us >= window_start_us &&
			             chunk_time_us < window_end_us) ? 1U : 0U;
			if(in_window != 0U)
			{
				window_audio_chunks++;
				window_audio_bytes += chunk.data_size;
				if(chunk.data_size < window_audio_min)
				{
					window_audio_min = chunk.data_size;
				}
				if(chunk.data_size > window_audio_max)
				{
					window_audio_max = chunk.data_size;
				}
				if(info->audio_block_align == 0U ||
				   (chunk.data_size % info->audio_block_align) != 0U)
				{
					window_audio_misaligned++;
					printf("A MISALIGNED time_ms=%lu off=0x%08lX size=%lu align=%u\n",
					       (unsigned long)(chunk_time_us / 1000U),
					       (unsigned long)(chunk.data_offset - 8U),
					       (unsigned long)chunk.data_size,
					       (unsigned int)info->audio_block_align);
				}
			}
			audio_bytes += chunk.data_size;
			audio_chunks++;
		}
		else
		{
			unknown_chunks++;
		}
	}
	printf("chunks video=%lu audio=%lu other=%lu first_video=0x%08lX result=%d\n",
	       (unsigned long)video_chunks,
	       (unsigned long)audio_chunks,
	       (unsigned long)unknown_chunks,
	       (unsigned long)first_video_header,
	       result);
	if(trace_window != 0U)
	{
		printf("window video=%lu bad_soi=%lu bad_eoi=%lu audio=%lu bytes=%lu min=%lu max=%lu misaligned=%lu\n",
		       (unsigned long)window_video_chunks,
		       (unsigned long)window_bad_soi,
		       (unsigned long)window_bad_eoi,
		       (unsigned long)window_audio_chunks,
		       (unsigned long)window_audio_bytes,
		       (unsigned long)((window_audio_chunks != 0U) ? window_audio_min : 0U),
		       (unsigned long)window_audio_max,
		       (unsigned long)window_audio_misaligned);
	}
	fclose(file.handle);
	return (result == AVI_PARSER_EOF && video_chunks > 0U) ? 0 : 1;
}
