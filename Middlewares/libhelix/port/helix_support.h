#ifndef HELIX_SUPPORT_H
#define HELIX_SUPPORT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

void *helix_memcpy(void *dst, const void *src, size_t size);
void *helix_memmove(void *dst, const void *src, size_t size);
void *helix_malloc(unsigned int size);
void helix_free(void *ptr);

#ifdef __cplusplus
}
#endif

#endif
