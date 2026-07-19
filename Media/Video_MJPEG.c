#include "Video_MJPEG.h"

#include "LCD.h"
#include "Video_Memory.h"
#include "jerror.h"
#include "jpeg_memory_provider.h"
#include "libjpeg.h"

#include <setjmp.h>
#include <string.h>

#define VIDEO_MJPEG_MAX_HEIGHT        240U
#define VIDEO_MJPEG_RGB_COMPONENTS    3U
typedef struct Video_JpegError
{
	struct jpeg_error_mgr pub;
	jmp_buf jump_buffer;
	volatile int *message_code;
} Video_JpegError_t;

typedef struct Video_JpegSource
{
	struct jpeg_source_mgr pub;
	FIL *file;
	u8 *buffer;
	u32 buffer_size;
	u32 remaining;
	Video_MJPEG_t *decoder;
	u8 injected_eoi;
} Video_JpegSource_t;

static void video_mjpeg_error_exit(j_common_ptr cinfo);
static void video_mjpeg_init_source(j_decompress_ptr cinfo);
static boolean video_mjpeg_fill_input_buffer(j_decompress_ptr cinfo);
static void video_mjpeg_skip_input_data(j_decompress_ptr cinfo, long byte_count);
static void video_mjpeg_term_source(j_decompress_ptr cinfo);
static void video_mjpeg_set_source(j_decompress_ptr cinfo, Video_JpegSource_t *source,
	Video_MJPEG_t *decoder, FIL *file, u32 size);
static void video_mjpeg_convert_row(u16 *destination, const u8 *source, u16 width);
static void *video_mjpeg_memory_alloc(void *user_data, size_t size);
static void video_mjpeg_memory_free(void *user_data, void *pointer);

static const JPEG_MemoryProvider_t s_video_jpeg_memory_provider =
{
	.magic = JPEG_MEMORY_PROVIDER_MAGIC,
	.alloc = video_mjpeg_memory_alloc,
	.free = video_mjpeg_memory_free,
	.user_data = NULL,
};

int Video_MJPEG_Init(Video_MJPEG_t *decoder)
{
	u32 row_size;
	u32 stripe_size;

	if(decoder == NULL)
	{
		return 1;
	}
	memset(decoder, 0, sizeof(*decoder));
	row_size = VIDEO_MJPEG_MAX_WIDTH * VIDEO_MJPEG_RGB_COMPONENTS;
	stripe_size = VIDEO_MJPEG_MAX_WIDTH * VIDEO_MJPEG_STRIPE_LINES * sizeof(u16);

	decoder->source_buffer = (u8 *)Video_Memory_Alloc(
		VIDEO_MJPEG_SOURCE_BUFFER_SIZE);
	decoder->row_buffer = (u8 *)Video_Memory_Alloc(row_size);
	decoder->stripe[0] = (u16 *)Video_Memory_Alloc(stripe_size);
	decoder->stripe[1] = (u16 *)Video_Memory_Alloc(stripe_size);
	decoder->jpeg_context = Video_Memory_Alloc(sizeof(struct jpeg_decompress_struct));
	decoder->jpeg_error = Video_Memory_Alloc(sizeof(Video_JpegError_t));
	if(decoder->source_buffer == NULL || decoder->row_buffer == NULL ||
	   decoder->stripe[0] == NULL || decoder->stripe[1] == NULL ||
	   decoder->jpeg_context == NULL || decoder->jpeg_error == NULL)
	{
		Video_MJPEG_Close(decoder);
		return 1;
	}

	return 0;
}

void Video_MJPEG_Close(Video_MJPEG_t *decoder)
{
	if(decoder == NULL)
	{
		return;
	}
	if(decoder->source_buffer != NULL)
	{
		Video_Memory_Free(decoder->source_buffer);
	}
	if(decoder->row_buffer != NULL)
	{
		Video_Memory_Free(decoder->row_buffer);
	}
	if(decoder->stripe[0] != NULL)
	{
		Video_Memory_Free(decoder->stripe[0]);
	}
	if(decoder->stripe[1] != NULL)
	{
		Video_Memory_Free(decoder->stripe[1]);
	}
	if(decoder->jpeg_context != NULL)
	{
		Video_Memory_Free(decoder->jpeg_context);
	}
	if(decoder->jpeg_error != NULL)
	{
		Video_Memory_Free(decoder->jpeg_error);
	}
	memset(decoder, 0, sizeof(*decoder));
}

Video_MJPEGResult_t Video_MJPEG_DecodeFrame(Video_MJPEG_t *decoder,
	FIL *file, const AVI_Chunk_t *chunk, u16 expected_width, u16 expected_height)
{
	struct jpeg_decompress_struct *cinfo;
	Video_JpegError_t *jerr;
	Video_JpegSource_t source;
	JSAMPROW row_pointer[1];
	u16 *stripe;
	u16 width;
	u16 height;
	u16 stripe_rows = 0U;
	u8 stripe_index = 0U;
	u16 x;
	u16 y;
	volatile int jpeg_message_code = 0;
	volatile u8 stream_started = 0U;
	Video_MJPEGResult_t result = VIDEO_MJPEG_FORMAT_ERROR;

	if(decoder == NULL || file == NULL || chunk == NULL ||
	   decoder->source_buffer == NULL || decoder->row_buffer == NULL ||
	   decoder->stripe[0] == NULL || decoder->stripe[1] == NULL ||
	   decoder->jpeg_context == NULL || decoder->jpeg_error == NULL ||
	   chunk->data_size == 0U)
	{
		return VIDEO_MJPEG_FORMAT_ERROR;
	}
	if(f_lseek(file, chunk->data_offset) != FR_OK)
	{
		decoder->fatfs_read_errors++;
		return VIDEO_MJPEG_IO_ERROR;
	}

	cinfo = (struct jpeg_decompress_struct *)decoder->jpeg_context;
	jerr = (Video_JpegError_t *)decoder->jpeg_error;
	memset(cinfo, 0, sizeof(*cinfo));
	memset(jerr, 0, sizeof(*jerr));
	memset(&source, 0, sizeof(source));
	cinfo->err = jpeg_std_error(&jerr->pub);
	cinfo->client_data = (void *)&s_video_jpeg_memory_provider;
	jerr->pub.error_exit = video_mjpeg_error_exit;
	jerr->message_code = &jpeg_message_code;

	if(setjmp(jerr->jump_buffer) != 0)
	{
		if(jpeg_message_code == (int)JERR_FILE_READ)
		{
			result = VIDEO_MJPEG_IO_ERROR;
		}
		else if(jpeg_message_code == (int)JERR_OUT_OF_MEMORY)
		{
			result = VIDEO_MJPEG_MEMORY_ERROR;
		}
		else
		{
			result = VIDEO_MJPEG_FORMAT_ERROR;
		}
		goto cleanup;
	}

	jpeg_create_decompress(cinfo);
	video_mjpeg_set_source(cinfo, &source, decoder, file, chunk->data_size);
	if(jpeg_read_header(cinfo, TRUE) != JPEG_HEADER_OK)
	{
		result = VIDEO_MJPEG_FORMAT_ERROR;
		goto cleanup;
	}
	if(cinfo->image_width == 0U || cinfo->image_height == 0U ||
	   cinfo->image_width > VIDEO_MJPEG_MAX_WIDTH ||
	   cinfo->image_height > VIDEO_MJPEG_MAX_HEIGHT ||
	   cinfo->image_width != expected_width || cinfo->image_height != expected_height)
	{
		result = VIDEO_MJPEG_SIZE_ERROR;
		goto cleanup;
	}

	cinfo->out_color_space = JCS_RGB;
	cinfo->dct_method = JDCT_IFAST;
	cinfo->do_fancy_upsampling = FALSE;
	if(jpeg_start_decompress(cinfo) == FALSE ||
	   cinfo->output_width != expected_width ||
	   cinfo->output_height != expected_height ||
	   cinfo->output_components != VIDEO_MJPEG_RGB_COMPONENTS)
	{
		result = VIDEO_MJPEG_FORMAT_ERROR;
		goto cleanup;
	}

	width = (u16)cinfo->output_width;
	height = (u16)cinfo->output_height;
	x = (u16)((VIDEO_MJPEG_MAX_WIDTH - width) / 2U);
	y = (u16)((VIDEO_MJPEG_MAX_HEIGHT - height) / 2U);
	if(LCD_StreamBegin(x, y, width, height) != 0U)
	{
		result = VIDEO_MJPEG_DISPLAY_ERROR;
		goto cleanup;
	}
	stream_started = 1U;
	row_pointer[0] = (JSAMPROW)decoder->row_buffer;

	while(cinfo->output_scanline < cinfo->output_height)
	{
		if(jpeg_read_scanlines(cinfo, row_pointer, 1U) != 1U)
		{
			result = VIDEO_MJPEG_FORMAT_ERROR;
			goto cleanup;
		}

		stripe = decoder->stripe[stripe_index];
		video_mjpeg_convert_row(&stripe[(u32)stripe_rows * width],
		                         decoder->row_buffer, width);
		stripe_rows++;
		if(stripe_rows >= VIDEO_MJPEG_STRIPE_LINES ||
		   cinfo->output_scanline == cinfo->output_height)
		{
			if(LCD_StreamWriteAsync(stripe, (u32)stripe_rows * width) != 0U)
			{
				result = VIDEO_MJPEG_DISPLAY_ERROR;
				goto cleanup;
			}
			stripe_index ^= 1U;
			stripe_rows = 0U;
		}
	}

	if(LCD_StreamWait() != 0U)
	{
		result = VIDEO_MJPEG_DISPLAY_ERROR;
		goto cleanup;
	}
	if(jpeg_finish_decompress(cinfo) == FALSE)
	{
		result = VIDEO_MJPEG_FORMAT_ERROR;
		goto cleanup;
	}
	result = VIDEO_MJPEG_OK;

cleanup:
	if(stream_started != 0U)
	{
		LCD_StreamEnd();
	}
	if(cinfo->mem != NULL)
	{
		jpeg_destroy_decompress(cinfo);
	}
	return result;
}

static void video_mjpeg_error_exit(j_common_ptr cinfo)
{
	Video_JpegError_t *error = (Video_JpegError_t *)cinfo->err;

	if(error != NULL)
	{
		if(error->message_code != NULL)
		{
			*error->message_code = cinfo->err->msg_code;
		}
		longjmp(error->jump_buffer, 1);
	}
	while(1)
	{
	}
}

static void video_mjpeg_init_source(j_decompress_ptr cinfo)
{
	Video_JpegSource_t *source = (Video_JpegSource_t *)cinfo->src;

	source->injected_eoi = 0U;
}

static boolean video_mjpeg_fill_input_buffer(j_decompress_ptr cinfo)
{
	Video_JpegSource_t *source = (Video_JpegSource_t *)cinfo->src;
	UINT bytes_read = 0U;
	u32 read_size;

	if(source->remaining == 0U)
	{
		if(source->injected_eoi != 0U)
		{
			ERREXIT(cinfo, JERR_INPUT_EOF);
		}
		source->buffer[0] = 0xFFU;
		source->buffer[1] = (u8)JPEG_EOI;
		source->pub.next_input_byte = (const JOCTET *)source->buffer;
		source->pub.bytes_in_buffer = 2U;
		source->injected_eoi = 1U;
		source->decoder->jpeg_eoi_injected++;
		return TRUE;
	}

	read_size = (source->remaining < source->buffer_size) ?
		source->remaining : source->buffer_size;
	if(f_read(source->file, source->buffer, (UINT)read_size, &bytes_read) != FR_OK ||
	   bytes_read == 0U || bytes_read > read_size)
	{
		source->decoder->fatfs_read_errors++;
		ERREXIT(cinfo, JERR_FILE_READ);
	}

	source->remaining -= (u32)bytes_read;
	source->pub.next_input_byte = (const JOCTET *)source->buffer;
	source->pub.bytes_in_buffer = (size_t)bytes_read;
	return TRUE;
}

static void video_mjpeg_skip_input_data(j_decompress_ptr cinfo, long byte_count)
{
	struct jpeg_source_mgr *source = cinfo->src;

	if(byte_count <= 0L)
	{
		return;
	}
	while(byte_count > (long)source->bytes_in_buffer)
	{
		byte_count -= (long)source->bytes_in_buffer;
		(void)(*source->fill_input_buffer)(cinfo);
	}
	source->next_input_byte += (size_t)byte_count;
	source->bytes_in_buffer -= (size_t)byte_count;
}

static void video_mjpeg_term_source(j_decompress_ptr cinfo)
{
	(void)cinfo;
}

static void video_mjpeg_set_source(j_decompress_ptr cinfo, Video_JpegSource_t *source,
	Video_MJPEG_t *decoder, FIL *file, u32 size)
{
	source->pub.init_source = video_mjpeg_init_source;
	source->pub.fill_input_buffer = video_mjpeg_fill_input_buffer;
	source->pub.skip_input_data = video_mjpeg_skip_input_data;
	source->pub.resync_to_restart = jpeg_resync_to_restart;
	source->pub.term_source = video_mjpeg_term_source;
	source->pub.bytes_in_buffer = 0U;
	source->pub.next_input_byte = NULL;
	source->file = file;
	source->buffer = decoder->source_buffer;
	source->buffer_size = VIDEO_MJPEG_SOURCE_BUFFER_SIZE;
	source->remaining = size;
	source->decoder = decoder;
	cinfo->src = &source->pub;
}

static void video_mjpeg_convert_row(u16 *destination, const u8 *source, u16 width)
{
	u16 x;
	u16 color;

	for(x = 0U; x < width; x++)
	{
		color = (u16)(((u16)(source[0] & 0xF8U) << 8U) |
		              ((u16)(source[1] & 0xFCU) << 3U) |
		              ((u16)source[2] >> 3U));
		destination[x] = (u16)((color << 8U) | (color >> 8U));
		source += VIDEO_MJPEG_RGB_COMPONENTS;
	}
}

static void *video_mjpeg_memory_alloc(void *user_data, size_t size)
{
	(void)user_data;
	if(size > (size_t)0xFFFFFFFFUL)
	{
		return NULL;
	}
	return Video_Memory_Alloc((u32)size);
}

static void video_mjpeg_memory_free(void *user_data, void *pointer)
{
	(void)user_data;
	Video_Memory_Free(pointer);
}
