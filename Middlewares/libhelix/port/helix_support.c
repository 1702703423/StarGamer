#include "helix_support.h"

#include "Malloc.h"

#include <string.h>

void *helix_memcpy(void *dst, const void *src, size_t size)
{
    return memcpy(dst, src, size);
}

void *helix_memmove(void *dst, const void *src, size_t size)
{
    return memmove(dst, src, size);
}

void *helix_malloc(unsigned int size)
{
    return sys_malloc(SRAMEX, (u32)size);
}

void helix_free(void *ptr)
{
    sys_free(SRAMEX, ptr);
}
