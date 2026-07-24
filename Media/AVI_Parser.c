#include "AVI_Parser.h"

#include <string.h>

#define AVI_FOURCC_RIFF       AVI_FOURCC('R', 'I', 'F', 'F')
#define AVI_FOURCC_AVI        AVI_FOURCC('A', 'V', 'I', ' ')
#define AVI_FOURCC_AVIX       AVI_FOURCC('A', 'V', 'I', 'X')
#define AVI_FOURCC_LIST       AVI_FOURCC('L', 'I', 'S', 'T')
#define AVI_FOURCC_HDRL       AVI_FOURCC('h', 'd', 'r', 'l')
#define AVI_FOURCC_MOVI       AVI_FOURCC('m', 'o', 'v', 'i')
#define AVI_FOURCC_STRL       AVI_FOURCC('s', 't', 'r', 'l')
#define AVI_FOURCC_STRH       AVI_FOURCC('s', 't', 'r', 'h')
#define AVI_FOURCC_STRF       AVI_FOURCC('s', 't', 'r', 'f')
#define AVI_FOURCC_VIDS       AVI_FOURCC('v', 'i', 'd', 's')
#define AVI_FOURCC_AUDS       AVI_FOURCC('a', 'u', 'd', 's')
#define AVI_FOURCC_MJPG       AVI_FOURCC('M', 'J', 'P', 'G')
#define AVI_FOURCC_REC        AVI_FOURCC('r', 'e', 'c', ' ')
#define AVI_FOURCC_INDX       AVI_FOURCC('i', 'n', 'd', 'x')

#define AVI_VIDEO_MAX_WIDTH   320U
#define AVI_VIDEO_MAX_HEIGHT  240U
#define AVI_VIDEO_MAX_FPS     120U
#define AVI_AUDIO_MIN_RATE    2000U
#define AVI_AUDIO_MAX_RATE    192000U
#define AVI_STRH_MIN_SIZE     48U
#define AVI_VIDEO_STRF_SIZE   40U
#define AVI_AUDIO_STRF_SIZE   16U

typedef struct AVI_InternalChunk
{
	u32 id;
	u32 data_offset;
	u32 data_size;
	u32 next_offset;
} AVI_InternalChunk_t;

static u16 avi_read_le16(const u8 *data);
static u32 avi_read_le32(const u8 *data);
static int avi_read_at(AVI_Parser_t *parser, u32 offset, void *buffer, u32 size);
static int avi_get_chunk(AVI_Parser_t *parser, u32 offset, u32 parent_end,
	AVI_InternalChunk_t *chunk);
static int avi_parse_hdrl(AVI_Parser_t *parser, u32 start, u32 end);
static int avi_parse_strl(AVI_Parser_t *parser, u32 start, u32 end, u8 stream_index);
static int avi_parse_video_stream(AVI_Parser_t *parser, const u8 *strh,
	u32 strf_offset, u32 strf_size, u8 stream_index);
static int avi_parse_audio_stream(AVI_Parser_t *parser, u32 strf_offset,
	u32 strf_size, u8 stream_index);
static u32 avi_upper_fourcc(u32 value);
static u8 avi_chunk_stream_index(u32 fourcc);
static u8 avi_is_index_chunk(u32 fourcc);
static u32 avi_permille_offset(u32 span, u16 position);
static int avi_find_video(AVI_Parser_t *parser, u32 scan_start,
	u32 scan_end, u32 parent_end, u8 prefer_last, u32 *found_offset);
static int avi_resync_video(AVI_Parser_t *parser, u32 start, u32 parent_end);
static void avi_record_error(AVI_Parser_t *parser, int result, u32 offset,
	u32 fourcc, u32 chunk_size);
static u32 avi_get_tick(const AVI_Parser_t *parser);

int AVI_Parser_Open(AVI_Parser_t *parser, FIL *file)
{
	u8 header[12];
	u8 list_type_data[4];
	AVI_InternalChunk_t chunk;
	u32 offset;
	u32 riff_size;
	u32 list_type;
	u8 found_hdrl = 0U;
	u8 found_movi = 0U;
	int result;

	if(parser == NULL || file == NULL)
	{
		return AVI_PARSER_BAD_CONTAINER;
	}

	memset(parser, 0, sizeof(*parser));
	parser->file = file;
	parser->file_size = (u32)f_size(file);
	parser->info.video_stream_index = AVI_STREAM_INDEX_NONE;
	parser->info.audio_stream_index = AVI_STREAM_INDEX_NONE;
	if(parser->file_size < sizeof(header))
	{
		return AVI_PARSER_BAD_CONTAINER;
	}

	result = avi_read_at(parser, 0U, header, sizeof(header));
	if(result != AVI_PARSER_OK)
	{
		return result;
	}
	if(avi_read_le32(&header[0]) != AVI_FOURCC_RIFF ||
	   avi_read_le32(&header[8]) != AVI_FOURCC_AVI)
	{
		return AVI_PARSER_BAD_CONTAINER;
	}

	riff_size = avi_read_le32(&header[4]);
	if(riff_size < 4U || riff_size > (parser->file_size - 8U))
	{
		return AVI_PARSER_BAD_CONTAINER;
	}
	parser->riff_end = 8U + riff_size;

	offset = 12U;
	while(offset < parser->riff_end)
	{
		result = avi_get_chunk(parser, offset, parser->riff_end, &chunk);
		if(result != AVI_PARSER_OK)
		{
			return result;
		}

		if(chunk.id == AVI_FOURCC_LIST)
		{
			if(chunk.data_size < 4U)
			{
				return AVI_PARSER_BAD_CHUNK;
			}
			result = avi_read_at(parser, chunk.data_offset, list_type_data,
			                     sizeof(list_type_data));
			if(result != AVI_PARSER_OK)
			{
				return result;
			}
			list_type = avi_read_le32(list_type_data);
			if(list_type == AVI_FOURCC_HDRL)
			{
				if(found_hdrl != 0U)
				{
					return AVI_PARSER_UNSUPPORTED;
				}
				result = avi_parse_hdrl(parser, chunk.data_offset + 4U,
				                        chunk.data_offset + chunk.data_size);
				if(result != AVI_PARSER_OK)
				{
					return result;
				}
				found_hdrl = 1U;
			}
			else if(list_type == AVI_FOURCC_MOVI)
			{
				if(found_movi != 0U)
				{
					return AVI_PARSER_UNSUPPORTED;
				}
				parser->movi_data_offset = chunk.data_offset + 4U;
				parser->movi_end = chunk.data_offset + chunk.data_size;
				found_movi = 1U;
			}
		}

		offset = chunk.next_offset;
	}

	if(found_hdrl == 0U || found_movi == 0U ||
	   parser->info.video_stream_index == AVI_STREAM_INDEX_NONE)
	{
		return AVI_PARSER_UNSUPPORTED;
	}
	if(parser->info.video_scale == 0U || parser->info.video_rate == 0U ||
	   parser->info.video_scale > (0xFFFFFFFFUL / 1000000UL))
	{
		return AVI_PARSER_UNSUPPORTED;
	}
	if(parser->info.video_scale <= (0xFFFFFFFFUL / AVI_VIDEO_MAX_FPS) &&
	   parser->info.video_rate > parser->info.video_scale * AVI_VIDEO_MAX_FPS)
	{
		return AVI_PARSER_UNSUPPORTED;
	}
	parser->info.frame_duration_us =
		(parser->info.video_scale * 1000000UL) /
		parser->info.video_rate;
	if(parser->info.frame_duration_us == 0U)
	{
		return AVI_PARSER_UNSUPPORTED;
	}

	if(parser->file_size > parser->riff_end &&
	   (parser->file_size - parser->riff_end) >= sizeof(header))
	{
		result = avi_read_at(parser, parser->riff_end, header, sizeof(header));
		if(result != AVI_PARSER_OK)
		{
			return result;
		}
		if(avi_read_le32(&header[0]) == AVI_FOURCC_RIFF &&
		   avi_read_le32(&header[8]) == AVI_FOURCC_AVIX)
		{
			return AVI_PARSER_UNSUPPORTED;
		}
	}

	return AVI_Parser_Rewind(parser);
}

int AVI_Parser_Rewind(AVI_Parser_t *parser)
{
	if(parser == NULL || parser->file == NULL ||
	   parser->movi_data_offset >= parser->movi_end)
	{
		return AVI_PARSER_BAD_CONTAINER;
	}

	parser->cursor = parser->movi_data_offset;
	parser->list_depth = 1U;
	parser->list_end[0] = parser->movi_end;
	parser->list_resume[0] = parser->movi_end;
	return AVI_PARSER_OK;
}

int AVI_Parser_SeekPermille(AVI_Parser_t *parser, u16 position)
{
	u32 span;
	u32 target;
	u32 scan_start;
	u32 scan_end;
	u32 found_offset;
	int result;

	if(parser == NULL || parser->file == NULL || position > 1000U ||
	   parser->movi_data_offset >= parser->movi_end)
	{
		return AVI_PARSER_BAD_CONTAINER;
	}
	if(position == 0U)
	{
		return AVI_Parser_Rewind(parser);
	}

	span = parser->movi_end - parser->movi_data_offset;
	target = parser->movi_data_offset + avi_permille_offset(span, position);
	target &= ~1UL;
	if(target < parser->movi_data_offset)
	{
		target = parser->movi_data_offset;
	}
	if(target > parser->movi_end)
	{
		target = parser->movi_end;
	}

	scan_end = parser->movi_end;
	if(target < scan_end && (scan_end - target) > AVI_RESYNC_SCAN_LIMIT)
	{
		scan_end = target + AVI_RESYNC_SCAN_LIMIT;
	}
	result = avi_find_video(parser, target, scan_end, parser->movi_end,
	                        0U, &found_offset);
	/* Near movi end, fall back to the closest earlier frame in the same bound. */
	if(result == AVI_PARSER_BAD_CHUNK &&
	   target > parser->movi_data_offset)
	{
		scan_start = parser->movi_data_offset;
		if((target - scan_start) > AVI_RESYNC_SCAN_LIMIT)
		{
			scan_start = target - AVI_RESYNC_SCAN_LIMIT;
		}
		scan_end = parser->movi_end;
		if((scan_end - target) > 10U)
		{
			scan_end = target + 10U;
		}
		result = avi_find_video(parser, scan_start, scan_end,
		                        parser->movi_end, 1U, &found_offset);
	}
	if(result != AVI_PARSER_OK)
	{
		avi_record_error(parser, result, target, 0U, 0U);
		return result;
	}

	parser->last_resync_from = target;
	parser->last_resync_to = found_offset;
	parser->resync_count++;
	parser->cursor = found_offset;
	parser->list_depth = 1U;
	parser->list_end[0] = parser->movi_end;
	parser->list_resume[0] = parser->movi_end;
	return AVI_PARSER_OK;
}

int AVI_Parser_Next(AVI_Parser_t *parser, AVI_Chunk_t *chunk)
{
	AVI_InternalChunk_t current;
	u8 type_data[4];
	u32 list_type;
	u32 parent_end;
	u8 stream_index;
	u8 suffix2;
	u8 suffix3;
	u8 retry;
	int result;

	if(parser == NULL || chunk == NULL || parser->file == NULL)
	{
		return AVI_PARSER_BAD_CONTAINER;
	}

	memset(chunk, 0, sizeof(*chunk));
	chunk->type = AVI_CHUNK_UNKNOWN;
	while(parser->list_depth > 0U)
	{
		parent_end = parser->list_end[parser->list_depth - 1U];
		if(parser->cursor >= parent_end)
		{
			parser->list_depth--;
			if(parser->list_depth == 0U)
			{
				return AVI_PARSER_EOF;
			}
			parser->cursor = parser->list_resume[parser->list_depth];
			continue;
		}

		memset(&current, 0, sizeof(current));
		result = avi_get_chunk(parser, parser->cursor, parent_end, &current);
		if(result == AVI_PARSER_BAD_CHUNK)
		{
			for(retry = 0U; retry < AVI_HEADER_RETRY_LIMIT; retry++)
			{
				memset(&current, 0, sizeof(current));
				result = avi_get_chunk(parser, parser->cursor, parent_end,
				                       &current);
				if(result == AVI_PARSER_OK)
				{
					parser->header_retry_count++;
					break;
				}
				if(result != AVI_PARSER_BAD_CHUNK)
				{
					break;
				}
			}
		}
		if(result != AVI_PARSER_OK)
		{
			if(result == AVI_PARSER_BAD_CHUNK &&
			   avi_resync_video(parser, parser->cursor, parent_end) ==
			   AVI_PARSER_OK)
			{
				continue;
			}
			avi_record_error(parser, result, parser->cursor, current.id,
			                 current.data_size);
			return result;
		}
		parser->cursor = current.next_offset;

		if(current.id == AVI_FOURCC_LIST)
		{
			if(current.data_size < 4U)
			{
				avi_record_error(parser, AVI_PARSER_BAD_CHUNK,
				                 current.data_offset - 8U, current.id,
				                 current.data_size);
				return AVI_PARSER_BAD_CHUNK;
			}
			result = avi_read_at(parser, current.data_offset, type_data, sizeof(type_data));
			if(result != AVI_PARSER_OK)
			{
				return result;
			}
			list_type = avi_read_le32(type_data);
			if(list_type == AVI_FOURCC_REC)
			{
				if(parser->list_depth >= AVI_MAX_LIST_DEPTH)
				{
					avi_record_error(parser, AVI_PARSER_BAD_CHUNK,
					                 current.data_offset - 8U, current.id,
					                 current.data_size);
					return AVI_PARSER_BAD_CHUNK;
				}
				parser->list_end[parser->list_depth] = current.data_offset + current.data_size;
				parser->list_resume[parser->list_depth] = current.next_offset;
				parser->list_depth++;
				parser->cursor = current.data_offset + 4U;
			}
			continue;
		}
		if(avi_is_index_chunk(current.id) != 0U)
		{
			parser->skipped_index_chunks++;
			continue;
		}

		chunk->fourcc = current.id;
		chunk->data_offset = current.data_offset;
		chunk->data_size = current.data_size;
		stream_index = avi_chunk_stream_index(current.id);
		suffix2 = (u8)(current.id >> 16U);
		suffix3 = (u8)(current.id >> 24U);
		if(stream_index == parser->info.video_stream_index &&
		   suffix2 == (u8)'d' && suffix3 == (u8)'c')
		{
			chunk->type = AVI_CHUNK_VIDEO;
		}
		else if(stream_index == parser->info.audio_stream_index &&
		        suffix2 == (u8)'w' && suffix3 == (u8)'b')
		{
			chunk->type = AVI_CHUNK_AUDIO;
		}
		else if(stream_index == parser->info.video_stream_index &&
		        suffix2 == (u8)'p' && suffix3 == (u8)'c')
		{
			chunk->type = AVI_CHUNK_PALETTE;
		}
		if(current.data_size > AVI_MAX_CHUNK_SIZE)
		{
			chunk->oversized = 1U;
			parser->oversized_chunks++;
		}

		return AVI_PARSER_OK;
	}

	return AVI_PARSER_EOF;
}

u16 AVI_Parser_GetPositionPermille(const AVI_Parser_t *parser)
{
	u32 span;
	u32 relative;
	u16 low = 0U;
	u16 high = 1000U;
	u16 middle;

	if(parser == NULL || parser->movi_data_offset >= parser->movi_end ||
	   parser->cursor <= parser->movi_data_offset)
	{
		return 0U;
	}
	if(parser->cursor >= parser->movi_end)
	{
		return 1000U;
	}

	span = parser->movi_end - parser->movi_data_offset;
	relative = parser->cursor - parser->movi_data_offset;
	while(low < high)
	{
		middle = (u16)((low + high + 1U) / 2U);
		if(avi_permille_offset(span, middle) <= relative)
		{
			low = middle;
		}
		else
		{
			high = (u16)(middle - 1U);
		}
	}
	return low;
}

const AVI_Info_t *AVI_Parser_GetInfo(const AVI_Parser_t *parser)
{
	return (parser != NULL) ? &parser->info : NULL;
}

void AVI_Parser_SetTickCallback(AVI_Parser_t *parser,
	AVI_GetTickCallback_t callback)
{
	if(parser != NULL)
	{
		parser->get_tick = callback;
	}
}

void AVI_IOTrace_Begin(AVI_IOTrace_t *trace, u32 operation,
	u32 offset, u32 request_bytes, u32 now)
{
	if(trace == NULL)
	{
		return;
	}
	trace->active_offset = offset;
	trace->active_request_bytes = request_bytes;
	trace->active_start_tick = now;
	__DMB();
	trace->active_operation = operation;
	__DMB();
}

void AVI_IOTrace_End(AVI_IOTrace_t *trace, u32 now)
{
	u32 elapsed;

	if(trace == NULL)
	{
		return;
	}
	elapsed = now - trace->active_start_tick;
	trace->last_ticks = elapsed;
	if(elapsed > trace->max_ticks)
	{
		trace->max_ticks = elapsed;
	}
	if(elapsed >= AVI_IO_SLOW_TICKS)
	{
		trace->slow_count++;
	}
	__DMB();
	trace->active_operation = AVI_IO_OP_NONE;
	__DMB();
}

static u16 avi_read_le16(const u8 *data)
{
	return (u16)data[0] | ((u16)data[1] << 8U);
}

static u32 avi_read_le32(const u8 *data)
{
	return (u32)data[0] |
	       ((u32)data[1] << 8U) |
	       ((u32)data[2] << 16U) |
	       ((u32)data[3] << 24U);
}

static int avi_read_at(AVI_Parser_t *parser, u32 offset, void *buffer, u32 size)
{
	FRESULT fatfs_result;
	UINT bytes_read = 0U;
	u32 now;

	if(parser == NULL || parser->file == NULL || buffer == NULL ||
	   offset > parser->file_size || size > (parser->file_size - offset))
	{
		return AVI_PARSER_BAD_CHUNK;
	}
	if((u32)f_tell(parser->file) != offset)
	{
		now = avi_get_tick(parser);
		AVI_IOTrace_Begin(&parser->io_trace, AVI_IO_OP_SEEK,
		                  offset, 0U, now);
		fatfs_result = f_lseek(parser->file, offset);
		AVI_IOTrace_End(&parser->io_trace, avi_get_tick(parser));
		if(fatfs_result != FR_OK)
		{
			return AVI_PARSER_IO_ERROR;
		}
	}
	now = avi_get_tick(parser);
	AVI_IOTrace_Begin(&parser->io_trace, AVI_IO_OP_READ,
	                  offset, size, now);
	fatfs_result = f_read(parser->file, buffer, (UINT)size, &bytes_read);
	AVI_IOTrace_End(&parser->io_trace, avi_get_tick(parser));
	if(fatfs_result != FR_OK ||
	   bytes_read != (UINT)size)
	{
		return AVI_PARSER_IO_ERROR;
	}
	return AVI_PARSER_OK;
}

static u32 avi_get_tick(const AVI_Parser_t *parser)
{
	return (parser != NULL && parser->get_tick != NULL) ?
	       parser->get_tick() : 0U;
}

static int avi_get_chunk(AVI_Parser_t *parser, u32 offset, u32 parent_end,
	AVI_InternalChunk_t *chunk)
{
	u8 header[8];
	u32 padded_size;
	int result;

	if(parser == NULL || chunk == NULL || offset > parent_end ||
	   (parent_end - offset) < sizeof(header))
	{
		return AVI_PARSER_BAD_CHUNK;
	}
	result = avi_read_at(parser, offset, header, sizeof(header));
	if(result != AVI_PARSER_OK)
	{
		return result;
	}

	chunk->id = avi_read_le32(&header[0]);
	chunk->data_size = avi_read_le32(&header[4]);
	chunk->data_offset = offset + (u32)sizeof(header);
	padded_size = chunk->data_size + (chunk->data_size & 1U);
	if(padded_size < chunk->data_size ||
	   padded_size > (parent_end - chunk->data_offset))
	{
		return AVI_PARSER_BAD_CHUNK;
	}
	chunk->next_offset = chunk->data_offset + padded_size;
	return AVI_PARSER_OK;
}

static int avi_parse_hdrl(AVI_Parser_t *parser, u32 start, u32 end)
{
	AVI_InternalChunk_t chunk;
	u8 type_data[4];
	u32 offset = start;
	u32 list_type;
	u8 stream_index = 0U;
	int result;

	while(offset < end)
	{
		result = avi_get_chunk(parser, offset, end, &chunk);
		if(result != AVI_PARSER_OK)
		{
			return result;
		}
		if(chunk.id == AVI_FOURCC_LIST)
		{
			if(chunk.data_size < 4U)
			{
				return AVI_PARSER_BAD_CHUNK;
			}
			result = avi_read_at(parser, chunk.data_offset, type_data, sizeof(type_data));
			if(result != AVI_PARSER_OK)
			{
				return result;
			}
			list_type = avi_read_le32(type_data);
			if(list_type == AVI_FOURCC_STRL)
			{
				if(stream_index >= AVI_MAX_STREAMS)
				{
					return AVI_PARSER_UNSUPPORTED;
				}
				result = avi_parse_strl(parser, chunk.data_offset + 4U,
				                        chunk.data_offset + chunk.data_size,
				                        stream_index);
				if(result != AVI_PARSER_OK)
				{
					return result;
				}
				stream_index++;
			}
		}
		offset = chunk.next_offset;
	}

	return AVI_PARSER_OK;
}

static int avi_parse_strl(AVI_Parser_t *parser, u32 start, u32 end, u8 stream_index)
{
	AVI_InternalChunk_t chunk;
	u8 strh[AVI_STRH_MIN_SIZE];
	u32 strf_offset = 0U;
	u32 strf_size = 0U;
	u32 offset = start;
	u8 have_strh = 0U;
	int result;

	memset(strh, 0, sizeof(strh));
	while(offset < end)
	{
		result = avi_get_chunk(parser, offset, end, &chunk);
		if(result != AVI_PARSER_OK)
		{
			return result;
		}
		if(chunk.id == AVI_FOURCC_STRH && have_strh == 0U)
		{
			if(chunk.data_size < AVI_STRH_MIN_SIZE)
			{
				return AVI_PARSER_BAD_CHUNK;
			}
			result = avi_read_at(parser, chunk.data_offset, strh, sizeof(strh));
			if(result != AVI_PARSER_OK)
			{
				return result;
			}
			have_strh = 1U;
		}
		else if(chunk.id == AVI_FOURCC_STRF && strf_size == 0U)
		{
			strf_offset = chunk.data_offset;
			strf_size = chunk.data_size;
		}
		offset = chunk.next_offset;
	}

	if(have_strh == 0U)
	{
		return AVI_PARSER_OK;
	}
	if(avi_read_le32(&strh[0]) == AVI_FOURCC_VIDS)
	{
		return avi_parse_video_stream(parser, strh, strf_offset, strf_size,
		                              stream_index);
	}
	if(avi_read_le32(&strh[0]) == AVI_FOURCC_AUDS)
	{
		return avi_parse_audio_stream(parser, strf_offset, strf_size, stream_index);
	}

	return AVI_PARSER_OK;
}

static int avi_parse_video_stream(AVI_Parser_t *parser, const u8 *strh,
	u32 strf_offset, u32 strf_size, u8 stream_index)
{
	u8 strf[AVI_VIDEO_STRF_SIZE];
	u32 handler;
	u32 compression;
	s32 width;
	s32 height;
	int result;

	if(parser->info.video_stream_index != AVI_STREAM_INDEX_NONE ||
	   strf_size < sizeof(strf))
	{
		return AVI_PARSER_OK;
	}
	result = avi_read_at(parser, strf_offset, strf, sizeof(strf));
	if(result != AVI_PARSER_OK)
	{
		return result;
	}
	if(avi_read_le32(&strf[0]) < AVI_VIDEO_STRF_SIZE)
	{
		return AVI_PARSER_OK;
	}

	handler = avi_upper_fourcc(avi_read_le32(&strh[4]));
	compression = avi_upper_fourcc(avi_read_le32(&strf[16]));
	if(handler != AVI_FOURCC_MJPG && compression != AVI_FOURCC_MJPG)
	{
		return AVI_PARSER_OK;
	}

	width = (s32)avi_read_le32(&strf[4]);
	height = (s32)avi_read_le32(&strf[8]);
	if(width <= 0 || height <= 0 || width > (s32)AVI_VIDEO_MAX_WIDTH ||
	   height > (s32)AVI_VIDEO_MAX_HEIGHT)
	{
		return AVI_PARSER_UNSUPPORTED;
	}

	parser->info.video_handler = handler;
	parser->info.video_compression = compression;
	parser->info.video_scale = avi_read_le32(&strh[20]);
	parser->info.video_rate = avi_read_le32(&strh[24]);
	parser->info.video_length = avi_read_le32(&strh[32]);
	parser->info.width = (u16)width;
	parser->info.height = (u16)height;
	parser->info.video_stream_index = stream_index;
	return AVI_PARSER_OK;
}

static int avi_parse_audio_stream(AVI_Parser_t *parser, u32 strf_offset,
	u32 strf_size, u8 stream_index)
{
	u8 strf[AVI_AUDIO_STRF_SIZE];
	u32 sample_rate;
	u32 average_bytes_per_second;
	u32 expected_bytes_per_second;
	u16 format_tag;
	u16 channels;
	u16 block_align;
	u16 bits_per_sample;
	u16 expected_block_align;
	int result;

	parser->info.has_audio = 1U;
	if(strf_size < sizeof(strf))
	{
		return AVI_PARSER_OK;
	}
	result = avi_read_at(parser, strf_offset, strf, sizeof(strf));
	if(result != AVI_PARSER_OK)
	{
		return result;
	}

	format_tag = avi_read_le16(&strf[0]);
	channels = avi_read_le16(&strf[2]);
	sample_rate = avi_read_le32(&strf[4]);
	average_bytes_per_second = avi_read_le32(&strf[8]);
	block_align = avi_read_le16(&strf[12]);
	bits_per_sample = avi_read_le16(&strf[14]);
	if(parser->info.audio_format_tag == 0U)
	{
		parser->info.audio_format_tag = format_tag;
		parser->info.audio_channels = channels;
		parser->info.audio_sample_rate = sample_rate;
		parser->info.audio_average_bytes_per_second = average_bytes_per_second;
		parser->info.audio_block_align = block_align;
		parser->info.audio_bits_per_sample = bits_per_sample;
	}

	if(format_tag != AVI_AUDIO_FORMAT_PCM ||
	   (channels != 1U && channels != 2U) ||
	   (bits_per_sample != 8U && bits_per_sample != 16U) ||
	   sample_rate < AVI_AUDIO_MIN_RATE || sample_rate > AVI_AUDIO_MAX_RATE)
	{
		return AVI_PARSER_OK;
	}

	expected_block_align = (u16)(channels * (bits_per_sample / 8U));
	expected_bytes_per_second = sample_rate * (u32)expected_block_align;
	if(block_align != expected_block_align ||
	   average_bytes_per_second != expected_bytes_per_second)
	{
		return AVI_PARSER_OK;
	}

	if(parser->info.audio_stream_index == AVI_STREAM_INDEX_NONE)
	{
		parser->info.audio_stream_index = stream_index;
		parser->info.audio_supported = 1U;
		parser->info.audio_format_tag = format_tag;
		parser->info.audio_channels = channels;
		parser->info.audio_sample_rate = sample_rate;
		parser->info.audio_average_bytes_per_second = average_bytes_per_second;
		parser->info.audio_block_align = block_align;
		parser->info.audio_bits_per_sample = bits_per_sample;
	}

	return AVI_PARSER_OK;
}

static u32 avi_upper_fourcc(u32 value)
{
	u32 result = 0U;
	u32 shift;
	u8 character;

	for(shift = 0U; shift < 32U; shift += 8U)
	{
		character = (u8)(value >> shift);
		if(character >= (u8)'a' && character <= (u8)'z')
		{
			character = (u8)(character - ((u8)'a' - (u8)'A'));
		}
		result |= ((u32)character << shift);
	}
	return result;
}

static u8 avi_chunk_stream_index(u32 fourcc)
{
	u8 tens = (u8)fourcc;
	u8 ones = (u8)(fourcc >> 8U);

	if(tens < (u8)'0' || tens > (u8)'9' ||
	   ones < (u8)'0' || ones > (u8)'9')
	{
		return AVI_STREAM_INDEX_NONE;
	}
	return (u8)((tens - (u8)'0') * 10U + (ones - (u8)'0'));
}

static u8 avi_is_index_chunk(u32 fourcc)
{
	u8 suffix2;
	u8 suffix3;

	if(fourcc == AVI_FOURCC_INDX)
	{
		return 1U;
	}
	if((u8)fourcc != (u8)'i' || (u8)(fourcc >> 8U) != (u8)'x')
	{
		return 0U;
	}
	suffix2 = (u8)(fourcc >> 16U);
	suffix3 = (u8)(fourcc >> 24U);
	return (suffix2 >= (u8)'0' && suffix2 <= (u8)'9' &&
	        suffix3 >= (u8)'0' && suffix3 <= (u8)'9') ? 1U : 0U;
}

static u32 avi_permille_offset(u32 span, u16 position)
{
	u32 whole = span / 1000U;
	u32 remainder = span % 1000U;

	return whole * (u32)position +
	       (remainder * (u32)position) / 1000U;
}

static int avi_find_video(AVI_Parser_t *parser, u32 scan_start,
	u32 scan_end, u32 parent_end, u8 prefer_last, u32 *found_offset)
{
	u32 scan_offset;
	u32 scan_size;
	u32 advance;
	u32 relative;
	u32 candidate_offset;
	u32 data_offset;
	u32 data_size;
	u32 padded_size;
	u32 fourcc;
	u32 candidate = 0U;
	u8 stream_index;
	u8 suffix2;
	u8 suffix3;
	int result;

	if(parser == NULL || found_offset == NULL || scan_start > scan_end ||
	   scan_end > parent_end)
	{
		return AVI_PARSER_BAD_CHUNK;
	}
	scan_offset = scan_start;
	while(scan_offset < scan_end)
	{
		scan_size = scan_end - scan_offset;
		if(scan_size > AVI_RESYNC_BUFFER_SIZE)
		{
			scan_size = AVI_RESYNC_BUFFER_SIZE;
		}
		if(scan_size < 10U)
		{
			break;
		}
		result = avi_read_at(parser, scan_offset, parser->resync_buffer,
		                     scan_size);
		if(result != AVI_PARSER_OK)
		{
			return result;
		}

		for(relative = 0U; (relative + 10U) <= scan_size; relative += 2U)
		{
			fourcc = avi_read_le32(&parser->resync_buffer[relative]);
			stream_index = avi_chunk_stream_index(fourcc);
			suffix2 = (u8)(fourcc >> 16U);
			suffix3 = (u8)(fourcc >> 24U);
			if(stream_index != parser->info.video_stream_index ||
			   suffix2 != (u8)'d' || suffix3 != (u8)'c' ||
			   parser->resync_buffer[relative + 8U] != 0xFFU ||
			   parser->resync_buffer[relative + 9U] != 0xD8U)
			{
				continue;
			}

			candidate_offset = scan_offset + relative;
			data_offset = candidate_offset + 8U;
			data_size = avi_read_le32(&parser->resync_buffer[relative + 4U]);
			padded_size = data_size + (data_size & 1U);
			if(data_size == 0U || padded_size < data_size ||
			   data_offset > parent_end ||
			   padded_size > (parent_end - data_offset))
			{
				continue;
			}

			candidate = candidate_offset;
			if(prefer_last == 0U)
			{
				*found_offset = candidate;
				return AVI_PARSER_OK;
			}
		}

		advance = (scan_size - 8U) & ~1UL;
		if(advance == 0U)
		{
			break;
		}
		scan_offset += advance;
	}
	if(candidate != 0U)
	{
		*found_offset = candidate;
		return AVI_PARSER_OK;
	}
	return AVI_PARSER_BAD_CHUNK;
}

static int avi_resync_video(AVI_Parser_t *parser, u32 start, u32 parent_end)
{
	u32 scan_offset;
	u32 scan_end;
	u32 scan_size;
	u32 advance;
	u32 relative;
	u32 candidate_offset;
	u32 data_offset;
	u32 data_size;
	u32 padded_size;
	u32 fourcc;
	u8 stream_index;
	u8 suffix2;
	u8 suffix3;
	int result;

	if(parser == NULL || start > parent_end ||
	   (parent_end - start) < 12U)
	{
		return AVI_PARSER_BAD_CHUNK;
	}
	scan_offset = start + 2U;
	scan_end = parent_end;
	if((scan_end - scan_offset) > AVI_RESYNC_SCAN_LIMIT)
	{
		scan_end = scan_offset + AVI_RESYNC_SCAN_LIMIT;
	}

	while(scan_offset < scan_end)
	{
		scan_size = scan_end - scan_offset;
		if(scan_size > AVI_RESYNC_BUFFER_SIZE)
		{
			scan_size = AVI_RESYNC_BUFFER_SIZE;
		}
		if(scan_size < 10U)
		{
			break;
		}
		result = avi_read_at(parser, scan_offset, parser->resync_buffer,
		                     scan_size);
		if(result != AVI_PARSER_OK)
		{
			return result;
		}

		for(relative = 0U; (relative + 10U) <= scan_size; relative += 2U)
		{
			fourcc = avi_read_le32(&parser->resync_buffer[relative]);
			stream_index = avi_chunk_stream_index(fourcc);
			suffix2 = (u8)(fourcc >> 16U);
			suffix3 = (u8)(fourcc >> 24U);
			if(stream_index != parser->info.video_stream_index ||
			   suffix2 != (u8)'d' || suffix3 != (u8)'c' ||
			   parser->resync_buffer[relative + 8U] != 0xFFU ||
			   parser->resync_buffer[relative + 9U] != 0xD8U)
			{
				continue;
			}

			candidate_offset = scan_offset + relative;
			data_offset = candidate_offset + 8U;
			data_size = avi_read_le32(&parser->resync_buffer[relative + 4U]);
			padded_size = data_size + (data_size & 1U);
			if(data_size == 0U || padded_size < data_size ||
			   data_offset > parent_end ||
			   padded_size > (parent_end - data_offset))
			{
				continue;
			}

			parser->last_resync_from = start;
			parser->last_resync_to = candidate_offset;
			parser->resync_count++;
			parser->cursor = candidate_offset;
			return AVI_PARSER_OK;
		}

		advance = (scan_size - 8U) & ~1UL;
		if(advance == 0U)
		{
			break;
		}
		scan_offset += advance;
	}

	return AVI_PARSER_BAD_CHUNK;
}

static void avi_record_error(AVI_Parser_t *parser, int result, u32 offset,
	u32 fourcc, u32 chunk_size)
{
	if(parser == NULL)
	{
		return;
	}
	parser->last_error_result = (s32)result;
	parser->last_error_offset = offset;
	parser->last_error_fourcc = fourcc;
	parser->last_error_chunk_size = chunk_size;
}
