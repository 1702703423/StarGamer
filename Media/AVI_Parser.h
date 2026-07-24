#ifndef MEDIA_AVI_PARSER_H
#define MEDIA_AVI_PARSER_H

#include "ff.h"
#include "sys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AVI_MAX_LIST_DEPTH      8U
#define AVI_MAX_STREAMS         8U
#define AVI_MAX_CHUNK_SIZE      (2UL * 1024UL * 1024UL)
#define AVI_RESYNC_SCAN_LIMIT   (256UL * 1024UL)
#define AVI_RESYNC_BUFFER_SIZE  512U
#define AVI_HEADER_RETRY_LIMIT  2U
#define AVI_STREAM_INDEX_NONE   0xFFU
#define AVI_AUDIO_FORMAT_PCM    0x0001U

#define AVI_IO_OP_NONE          0U
#define AVI_IO_OP_SEEK          1U
#define AVI_IO_OP_READ          2U
#define AVI_IO_SLOW_TICKS       20U

#define AVI_FOURCC(a, b, c, d) \
	((u32)(u8)(a) | ((u32)(u8)(b) << 8U) | \
	 ((u32)(u8)(c) << 16U) | ((u32)(u8)(d) << 24U))

typedef enum
{
	AVI_PARSER_OK = 0,
	AVI_PARSER_EOF = 1,
	AVI_PARSER_IO_ERROR = -1,
	AVI_PARSER_BAD_CONTAINER = -2,
	AVI_PARSER_UNSUPPORTED = -3,
	AVI_PARSER_BAD_CHUNK = -4
} AVI_ParserResult_t;

typedef enum
{
	AVI_CHUNK_VIDEO = 0,
	AVI_CHUNK_AUDIO,
	AVI_CHUNK_PALETTE,
	AVI_CHUNK_UNKNOWN
} AVI_ChunkType_t;

typedef struct AVI_Chunk
{
	AVI_ChunkType_t type;
	u32 fourcc;
	u32 data_offset;
	u32 data_size;
	u8 oversized;
} AVI_Chunk_t;

typedef struct AVI_Info
{
	u32 video_handler;
	u32 video_compression;
	u32 video_scale;
	u32 video_rate;
	u32 video_length;
	u32 frame_duration_us;
	u32 audio_sample_rate;
	u32 audio_average_bytes_per_second;
	u16 width;
	u16 height;
	u16 audio_format_tag;
	u16 audio_channels;
	u16 audio_block_align;
	u16 audio_bits_per_sample;
	u8 video_stream_index;
	u8 audio_stream_index;
	u8 has_audio;
	u8 audio_supported;
} AVI_Info_t;

typedef struct AVI_IOTrace
{
	volatile u32 active_operation;
	volatile u32 active_offset;
	volatile u32 active_request_bytes;
	volatile u32 active_start_tick;
	u32 last_ticks;
	u32 max_ticks;
	u32 slow_count;
} AVI_IOTrace_t;

typedef u32 (*AVI_GetTickCallback_t)(void);

typedef struct AVI_Parser
{
	FIL *file;
	u32 file_size;
	u32 riff_end;
	u32 movi_data_offset;
	u32 movi_end;
	u32 cursor;
	u32 list_end[AVI_MAX_LIST_DEPTH];
	u32 list_resume[AVI_MAX_LIST_DEPTH];
	u32 last_error_offset;
	u32 last_error_fourcc;
	u32 last_error_chunk_size;
	u32 skipped_index_chunks;
	u32 oversized_chunks;
	u32 header_retry_count;
	u32 resync_count;
	u32 last_resync_from;
	u32 last_resync_to;
	s32 last_error_result;
	AVI_IOTrace_t io_trace;
	AVI_GetTickCallback_t get_tick;
	u8 resync_buffer[AVI_RESYNC_BUFFER_SIZE];
	u8 list_depth;
	AVI_Info_t info;
} AVI_Parser_t;

int AVI_Parser_Open(AVI_Parser_t *parser, FIL *file);
int AVI_Parser_Rewind(AVI_Parser_t *parser);
int AVI_Parser_SeekPermille(AVI_Parser_t *parser, u16 position);
int AVI_Parser_Next(AVI_Parser_t *parser, AVI_Chunk_t *chunk);
u16 AVI_Parser_GetPositionPermille(const AVI_Parser_t *parser);
const AVI_Info_t *AVI_Parser_GetInfo(const AVI_Parser_t *parser);
void AVI_Parser_SetTickCallback(AVI_Parser_t *parser,
	AVI_GetTickCallback_t callback);
void AVI_IOTrace_Begin(AVI_IOTrace_t *trace, u32 operation,
	u32 offset, u32 request_bytes, u32 now);
void AVI_IOTrace_End(AVI_IOTrace_t *trace, u32 now);

#ifdef __cplusplus
}
#endif

#endif
