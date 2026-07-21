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
	int result;

	if(argc != 2)
	{
		fprintf(stderr, "usage: avi_parser_host <file.avi>\n");
		return 2;
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
		if(chunk.data_offset > parser.file_size ||
		   chunk.data_size > (parser.file_size - chunk.data_offset))
		{
			fprintf(stderr, "chunk escaped file boundary\n");
			fclose(file.handle);
			return 1;
		}
		if(chunk.type == AVI_CHUNK_VIDEO)
		{
			if(video_chunks == 0U)
			{
				first_video_header = chunk.data_offset - 8U;
			}
			video_chunks++;
		}
		else if(chunk.type == AVI_CHUNK_AUDIO)
		{
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
	fclose(file.handle);
	return (result == AVI_PARSER_EOF && video_chunks > 0U) ? 0 : 1;
}
