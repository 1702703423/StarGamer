#include "AVI_Parser.h"

#include <stdio.h>
#include <string.h>

#define TEST_HDRL_OFFSET       12UL
#define TEST_MOVI_OFFSET       140UL
#define TEST_FIRST_CHUNK       152UL
#define TEST_LARGE_CHUNK_SIZE  (AVI_MAX_CHUNK_SIZE + 2UL)

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

static void put_le32(u8 *data, u32 value)
{
	data[0] = (u8)value;
	data[1] = (u8)(value >> 8U);
	data[2] = (u8)(value >> 16U);
	data[3] = (u8)(value >> 24U);
}

static void put_fourcc(u8 *data, char a, char b, char c, char d)
{
	data[0] = (u8)a;
	data[1] = (u8)b;
	data[2] = (u8)c;
	data[3] = (u8)d;
}

static void build_header(u8 *header, u32 movi_data_size)
{
	memset(header, 0, TEST_FIRST_CHUNK);
	put_fourcc(&header[0], 'R', 'I', 'F', 'F');
	put_le32(&header[4], 4U + 128U + 8U + movi_data_size);
	put_fourcc(&header[8], 'A', 'V', 'I', ' ');

	put_fourcc(&header[TEST_HDRL_OFFSET], 'L', 'I', 'S', 'T');
	put_le32(&header[TEST_HDRL_OFFSET + 4U], 120U);
	put_fourcc(&header[TEST_HDRL_OFFSET + 8U], 'h', 'd', 'r', 'l');
	put_fourcc(&header[24], 'L', 'I', 'S', 'T');
	put_le32(&header[28], 108U);
	put_fourcc(&header[32], 's', 't', 'r', 'l');

	put_fourcc(&header[36], 's', 't', 'r', 'h');
	put_le32(&header[40], 48U);
	put_fourcc(&header[44], 'v', 'i', 'd', 's');
	put_fourcc(&header[48], 'M', 'J', 'P', 'G');
	put_le32(&header[64], 1U);
	put_le32(&header[68], 12U);
	put_le32(&header[76], 1U);

	put_fourcc(&header[92], 's', 't', 'r', 'f');
	put_le32(&header[96], 40U);
	put_le32(&header[100], 40U);
	put_le32(&header[104], 320U);
	put_le32(&header[108], 240U);
	put_fourcc(&header[116], 'M', 'J', 'P', 'G');

	put_fourcc(&header[TEST_MOVI_OFFSET], 'L', 'I', 'S', 'T');
	put_le32(&header[TEST_MOVI_OFFSET + 4U], movi_data_size);
	put_fourcc(&header[TEST_MOVI_OFFSET + 8U], 'm', 'o', 'v', 'i');
}

static int finish_sparse_file(FIL *file, u32 last_offset, const u8 *tail,
	u32 tail_size)
{
	long size;

	if(fseek(file->handle, (long)last_offset, SEEK_SET) != 0 ||
	   fwrite(tail, 1U, tail_size, file->handle) != tail_size ||
	   fflush(file->handle) != 0 || fseek(file->handle, 0L, SEEK_END) != 0)
	{
		return 0;
	}
	size = ftell(file->handle);
	if(size < 0)
	{
		return 0;
	}
	file->size = (unsigned long)size;
	return (fseek(file->handle, 0L, SEEK_SET) == 0) ? 1 : 0;
}

static int test_large_index_skip(void)
{
	u8 header[TEST_FIRST_CHUNK + 8U];
	u8 video_chunk[10];
	u32 video_offset = TEST_FIRST_CHUNK + 8U + TEST_LARGE_CHUNK_SIZE;
	FIL file;
	AVI_Parser_t parser;
	AVI_Chunk_t chunk;
	int result;

	memset(&file, 0, sizeof(file));
	file.handle = tmpfile();
	if(file.handle == NULL)
	{
		return 0;
	}
	build_header(header, 4U + 8U + TEST_LARGE_CHUNK_SIZE + sizeof(video_chunk));
	put_fourcc(&header[TEST_FIRST_CHUNK], 'i', 'x', '0', '0');
	put_le32(&header[TEST_FIRST_CHUNK + 4U], TEST_LARGE_CHUNK_SIZE);
	memset(video_chunk, 0, sizeof(video_chunk));
	put_fourcc(&video_chunk[0], '0', '0', 'd', 'c');
	put_le32(&video_chunk[4], 2U);
	if(fwrite(header, 1U, sizeof(header), file.handle) != sizeof(header) ||
	   finish_sparse_file(&file, video_offset, video_chunk,
	                      sizeof(video_chunk)) == 0)
	{
		fclose(file.handle);
		return 0;
	}

	result = AVI_Parser_Open(&parser, &file);
	if(result == AVI_PARSER_OK)
	{
		result = AVI_Parser_Next(&parser, &chunk);
	}
	fclose(file.handle);
	return (result == AVI_PARSER_OK && chunk.type == AVI_CHUNK_VIDEO &&
	        chunk.oversized == 0U && parser.skipped_index_chunks == 1U) ? 1 : 0;
}

static int test_large_video_is_reported(void)
{
	u8 header[TEST_FIRST_CHUNK + 8U];
	u8 tail = 0U;
	u32 file_end = TEST_FIRST_CHUNK + 8U + TEST_LARGE_CHUNK_SIZE;
	FIL file;
	AVI_Parser_t parser;
	AVI_Chunk_t chunk;
	int result;

	memset(&file, 0, sizeof(file));
	file.handle = tmpfile();
	if(file.handle == NULL)
	{
		return 0;
	}
	build_header(header, 4U + 8U + TEST_LARGE_CHUNK_SIZE);
	put_fourcc(&header[TEST_FIRST_CHUNK], '0', '0', 'd', 'c');
	put_le32(&header[TEST_FIRST_CHUNK + 4U], TEST_LARGE_CHUNK_SIZE);
	if(fwrite(header, 1U, sizeof(header), file.handle) != sizeof(header) ||
	   finish_sparse_file(&file, file_end - 1U, &tail, 1U) == 0)
	{
		fclose(file.handle);
		return 0;
	}

	result = AVI_Parser_Open(&parser, &file);
	if(result == AVI_PARSER_OK)
	{
		result = AVI_Parser_Next(&parser, &chunk);
	}
	fclose(file.handle);
	return (result == AVI_PARSER_OK && chunk.type == AVI_CHUNK_VIDEO &&
	        chunk.oversized != 0U && parser.oversized_chunks == 1U) ? 1 : 0;
}

static int test_bad_chunk_resync(void)
{
	u8 header[TEST_FIRST_CHUNK];
	u8 chunks[40];
	FIL file;
	AVI_Parser_t parser;
	AVI_Chunk_t chunk;
	int result;

	memset(&file, 0, sizeof(file));
	file.handle = tmpfile();
	if(file.handle == NULL)
	{
		return 0;
	}
	build_header(header, 4U + sizeof(chunks));
	memset(chunks, 0, sizeof(chunks));
	put_fourcc(&chunks[0], '0', '0', 'd', 'c');
	put_le32(&chunks[4], 4U);
	chunks[8] = 0xFFU;
	chunks[9] = 0xD8U;
	chunks[10] = 0xFFU;
	chunks[11] = 0xD9U;
	put_fourcc(&chunks[12], 'B', 'A', 'D', '!');
	put_le32(&chunks[16], 0xFFFFFFFFUL);
	put_fourcc(&chunks[28], '0', '0', 'd', 'c');
	put_le32(&chunks[32], 4U);
	chunks[36] = 0xFFU;
	chunks[37] = 0xD8U;
	chunks[38] = 0xFFU;
	chunks[39] = 0xD9U;
	if(fwrite(header, 1U, sizeof(header), file.handle) != sizeof(header) ||
	   finish_sparse_file(&file, TEST_FIRST_CHUNK, chunks, sizeof(chunks)) == 0)
	{
		fclose(file.handle);
		return 0;
	}

	result = AVI_Parser_Open(&parser, &file);
	if(result == AVI_PARSER_OK)
	{
		result = AVI_Parser_Next(&parser, &chunk);
	}
	if(result == AVI_PARSER_OK)
	{
		result = AVI_Parser_Next(&parser, &chunk);
	}
	fclose(file.handle);
	return (result == AVI_PARSER_OK && chunk.type == AVI_CHUNK_VIDEO &&
	        chunk.data_offset == (TEST_FIRST_CHUNK + 36U) &&
	        parser.resync_count == 1U &&
	        parser.last_resync_from == (TEST_FIRST_CHUNK + 12U) &&
	        parser.last_resync_to == (TEST_FIRST_CHUNK + 28U)) ? 1 : 0;
}

int main(void)
{
	if(test_large_index_skip() == 0)
	{
		fprintf(stderr, "large index skip failed\n");
		return 1;
	}
	if(test_large_video_is_reported() == 0)
	{
		fprintf(stderr, "large video handling failed\n");
		return 1;
	}
	if(test_bad_chunk_resync() == 0)
	{
		fprintf(stderr, "bad chunk resync failed\n");
		return 1;
	}
	printf("AVI parser regression tests passed\n");
	return 0;
}
