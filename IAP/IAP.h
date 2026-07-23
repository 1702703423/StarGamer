#ifndef IAP_H
#define IAP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sys.h"
#include "IAP_Protocol.h"

typedef struct IAP_Record
{
	IAP_RECORD_MEMBERS;
} IAP_Record_t;

typedef char IAP_RecordSizeCheck[(sizeof(IAP_Record_t) == IAP_RECORD_SIZE) ? 1 : -1];

typedef enum IAP_Status
{
	IAP_STATUS_OK = 0,
	IAP_STATUS_INVALID_ARGUMENT,
	IAP_STATUS_FILE_NOT_FOUND,
	IAP_STATUS_FILE_SIZE_INVALID,
	IAP_STATUS_FILE_IO,
	IAP_STATUS_IMAGE_INVALID,
	IAP_STATUS_OUT_OF_MEMORY,
	IAP_STATUS_W25Q64_NOT_FOUND,
	IAP_STATUS_W25Q64_ERASE_FAILED,
	IAP_STATUS_W25Q64_WRITE_FAILED,
	IAP_STATUS_W25Q64_READ_FAILED,
	IAP_STATUS_CRC_MISMATCH,
	IAP_STATUS_EEPROM_FAILED,
	IAP_STATUS_RECORD_NOT_FOUND
} IAP_Status_t;

typedef void (*IAP_ProgressCallback_t)(u8 percent, void *context);

IAP_Status_t IAP_PrepareUpdate(IAP_ProgressCallback_t progress_cb, void *context);
IAP_Status_t IAP_RecordLoad(IAP_Record_t *record);
IAP_Status_t IAP_RecordSave(IAP_Record_t *record);
u32 IAP_Crc32(const u8 *data, u32 length);
const char *IAP_StatusString(IAP_Status_t status);

#ifdef __cplusplus
}
#endif

#endif
