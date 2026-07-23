#ifndef BOOT_STORAGE_H
#define BOOT_STORAGE_H

#include "boot_platform.h"
#include "IAP_Protocol.h"

typedef struct Boot_Record
{
	IAP_RECORD_MEMBERS;
} Boot_Record_t;

typedef char Boot_RecordSizeCheck[(sizeof(Boot_Record_t) == IAP_RECORD_SIZE) ? 1 : -1];

typedef enum Boot_RecordStatus
{
	BOOT_RECORD_OK = 0,
	BOOT_RECORD_EMPTY,
	BOOT_RECORD_IO_ERROR
} Boot_RecordStatus_t;

void Boot_RecordDefaults(Boot_Record_t *record);
Boot_RecordStatus_t Boot_RecordLoad(Boot_Record_t *record);
Boot_RecordStatus_t Boot_RecordSave(Boot_Record_t *record);

u8 Boot_W25_Init(void);
u8 Boot_W25_Read(u32 address, u8 *data, u32 length);
u8 Boot_W25_Write(u32 address, u8 *data, u32 length);
u8 Boot_W25_Erase64K(u32 address);

#endif
