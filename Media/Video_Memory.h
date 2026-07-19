#ifndef MEDIA_VIDEO_MEMORY_H
#define MEDIA_VIDEO_MEMORY_H

#include "sys.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VIDEO_MEMORY_ALIGNMENT  32U
#define VIDEO_MEMORY_POOL_SIZE  (80UL * 1024UL)

typedef struct Video_MemoryStats
{
	u32 pool_size;
	u32 current_bytes;
	u32 peak_bytes;
	u32 largest_free_block;
	u32 allocation_failures;
} Video_MemoryStats_t;

void Video_Memory_Init(void);
void *Video_Memory_Alloc(u32 size);
void Video_Memory_Free(void *pointer);
void Video_Memory_ResetStats(void);
void Video_Memory_GetStats(Video_MemoryStats_t *stats);
u8 Video_Memory_Owns(const void *pointer);

#ifdef __cplusplus
}
#endif

#endif
