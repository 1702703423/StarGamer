#ifndef AVI_PARSER_HOST_FF_H
#define AVI_PARSER_HOST_FF_H

#include <stdio.h>

typedef unsigned int UINT;

typedef enum
{
	FR_OK = 0,
	FR_DISK_ERR = 1
} FRESULT;

typedef struct
{
	FILE *handle;
	unsigned long size;
} FIL;

#define f_size(file) ((file)->size)

FRESULT f_lseek(FIL *file, unsigned long offset);
FRESULT f_read(FIL *file, void *buffer, UINT size, UINT *bytes_read);

#endif
