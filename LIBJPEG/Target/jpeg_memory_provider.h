#ifndef JPEG_MEMORY_PROVIDER_H
#define JPEG_MEMORY_PROVIDER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define JPEG_MEMORY_PROVIDER_MAGIC  0x4A4D454DUL

typedef void *(*JPEG_MemoryAlloc_t)(void *user_data, size_t size);
typedef void (*JPEG_MemoryFree_t)(void *user_data, void *pointer);

typedef struct JPEG_MemoryProvider
{
	unsigned long magic;
	JPEG_MemoryAlloc_t alloc;
	JPEG_MemoryFree_t free;
	void *user_data;
} JPEG_MemoryProvider_t;

#ifdef __cplusplus
}
#endif

#endif
