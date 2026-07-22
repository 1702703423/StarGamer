#ifndef MEDIA_VIDEO_MJPEG_H
#define MEDIA_VIDEO_MJPEG_H

#include "AVI_Parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_MJPEG_SOURCE_BUFFER_SIZE  (8UL * 1024UL)
#define VIDEO_MJPEG_ARENA_SIZE          (38UL * 1024UL)
#define VIDEO_MJPEG_STRIPE_LINES        16U
#define VIDEO_MJPEG_MAX_WIDTH           320U

typedef enum
{
	VIDEO_MJPEG_OK = 0,
	VIDEO_MJPEG_IO_ERROR,
	VIDEO_MJPEG_FORMAT_ERROR,
	VIDEO_MJPEG_SIZE_ERROR,
	VIDEO_MJPEG_MEMORY_ERROR,
	VIDEO_MJPEG_DISPLAY_ERROR
} Video_MJPEGResult_t;

typedef struct Video_MJPEG
{
	u8 *source_buffer;
	u8 *row_buffer;
	u16 *stripe[2];
	void *jpeg_context;
	void *jpeg_error;
	void *jpeg_provider;
	u8 *jpeg_arena;
	u32 jpeg_arena_position;
	u32 jpeg_arena_frame_base;
	u32 jpeg_arena_peak_bytes;
	u32 jpeg_arena_allocation_failures;
	u32 jpeg_eoi_injected;
	u32 fatfs_read_errors;
	u32 fatfs_read_calls;
	u32 fatfs_read_bytes;
	u32 lcd_wait_ticks;
	u32 max_lcd_wait_ticks;
	u8 jpeg_created;
} Video_MJPEG_t;

int Video_MJPEG_Init(Video_MJPEG_t *decoder);
void Video_MJPEG_Close(Video_MJPEG_t *decoder);
Video_MJPEGResult_t Video_MJPEG_DecodeFrame(Video_MJPEG_t *decoder,
	FIL *file, const AVI_Chunk_t *chunk, u16 expected_width, u16 expected_height);

#ifdef __cplusplus
}
#endif

#endif
