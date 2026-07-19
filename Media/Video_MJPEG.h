#ifndef MEDIA_VIDEO_MJPEG_H
#define MEDIA_VIDEO_MJPEG_H

#include "AVI_Parser.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_MJPEG_SOURCE_BUFFER_SIZE  4096U
#define VIDEO_MJPEG_STRIPE_LINES        8U
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
	u32 jpeg_eoi_injected;
	u32 fatfs_read_errors;
} Video_MJPEG_t;

int Video_MJPEG_Init(Video_MJPEG_t *decoder);
void Video_MJPEG_Close(Video_MJPEG_t *decoder);
Video_MJPEGResult_t Video_MJPEG_DecodeFrame(Video_MJPEG_t *decoder,
	FIL *file, const AVI_Chunk_t *chunk, u16 expected_width, u16 expected_height);

#ifdef __cplusplus
}
#endif

#endif
