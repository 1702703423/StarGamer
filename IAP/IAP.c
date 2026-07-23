#include "IAP.h"

#include "Malloc.h"
#include "at24c02.h"
#include "fatfs.h"
#include "w25q64.h"

#include <string.h>

#define IAP_TRANSFER_BUFFER_SIZE           4096U
#define IAP_W25Q64_BLOCK_SIZE              (64UL * 1024UL)
#define IAP_CRC_INITIAL                    0xFFFFFFFFUL
#define IAP_CRC_XOR_OUT                    0xFFFFFFFFUL

static const u32 s_crc32_nibble_table[16] =
{
	0x00000000UL, 0x1DB71064UL, 0x3B6E20C8UL, 0x26D930ACUL,
	0x76DC4190UL, 0x6B6B51F4UL, 0x4DB26158UL, 0x5005713CUL,
	0xEDB88320UL, 0xF00F9344UL, 0xD6D6A3E8UL, 0xCB61B38CUL,
	0x9B64C2B0UL, 0x86D3D2D4UL, 0xA00AE278UL, 0xBDBDF21CUL
};

static u32 IAP_Crc32Update(u32 crc, const u8 *data, u32 length);
static u32 IAP_RecordChecksum(const IAP_Record_t *record);
static u8 IAP_RecordValid(const IAP_Record_t *record);
static void IAP_RecordDefaults(IAP_Record_t *record);
static IAP_Status_t IAP_RecordLoadSlot(u8 address, IAP_Record_t *record);
static u8 IAP_SequenceNewer(u32 lhs, u32 rhs);
static u8 IAP_VectorValid(const u8 *header);
static IAP_Status_t IAP_W25Q64Crc(u32 address, u32 length, u8 *buffer, u32 *crc_out,
	IAP_ProgressCallback_t progress_cb, void *context);
static void IAP_ReportProgress(IAP_ProgressCallback_t progress_cb, void *context, u8 percent);

static u32 IAP_Crc32Update(u32 crc, const u8 *data, u32 length)
{
	u32 i;

	for (i = 0U; i < length; i++)
	{
		crc ^= data[i];
		crc = s_crc32_nibble_table[crc & 0x0FU] ^ (crc >> 4U);
		crc = s_crc32_nibble_table[crc & 0x0FU] ^ (crc >> 4U);
	}

	return crc;
}

u32 IAP_Crc32(const u8 *data, u32 length)
{
	if ((data == NULL) && (length != 0U))
	{
		return 0U;
	}

	return IAP_Crc32Update(IAP_CRC_INITIAL, data, length) ^ IAP_CRC_XOR_OUT;
}

static u32 IAP_RecordChecksum(const IAP_Record_t *record)
{
	const u8 *bytes = (const u8 *)record;
	u32 hash = 2166136261UL;
	u32 i;

	for (i = 0U; i < (IAP_RECORD_SIZE - sizeof(record->checksum)); i++)
	{
		hash ^= bytes[i];
		hash *= 16777619UL;
	}

	return hash;
}

static u8 IAP_RecordValid(const IAP_Record_t *record)
{
	if ((record->magic != IAP_RECORD_MAGIC) ||
	    (record->version != IAP_RECORD_VERSION) ||
	    (record->record_size != IAP_RECORD_SIZE) ||
	    (record->checksum != IAP_RecordChecksum(record)))
	{
		return 0U;
	}

	if ((record->staging_size > IAP_APP_MAX_SIZE) ||
	    ((record->backup_state == IAP_BACKUP_VALID) &&
	     ((record->backup_size == 0U) || (record->backup_size > IAP_APP_MAX_SIZE))))
	{
		return 0U;
	}

	return 1U;
}

static void IAP_RecordDefaults(IAP_Record_t *record)
{
	memset(record, 0, sizeof(*record));
	record->magic = IAP_RECORD_MAGIC;
	record->version = IAP_RECORD_VERSION;
	record->record_size = IAP_RECORD_SIZE;
}

static IAP_Status_t IAP_RecordLoadSlot(u8 address, IAP_Record_t *record)
{
	if (AT24C02_Read(address, (u8 *)record, (u16)sizeof(*record)) != AT24C02_OK)
	{
		return IAP_STATUS_EEPROM_FAILED;
	}

	return (IAP_RecordValid(record) != 0U) ? IAP_STATUS_OK : IAP_STATUS_RECORD_NOT_FOUND;
}

static u8 IAP_SequenceNewer(u32 lhs, u32 rhs)
{
	return (((s32)(lhs - rhs)) > 0) ? 1U : 0U;
}

IAP_Status_t IAP_RecordLoad(IAP_Record_t *record)
{
	IAP_Record_t slot0;
	IAP_Record_t slot1;
	IAP_Status_t status0;
	IAP_Status_t status1;

	if (record == NULL)
	{
		return IAP_STATUS_INVALID_ARGUMENT;
	}

	status0 = IAP_RecordLoadSlot(IAP_EEPROM_SLOT0_ADDRESS, &slot0);
	status1 = IAP_RecordLoadSlot(IAP_EEPROM_SLOT1_ADDRESS, &slot1);

	if ((status0 == IAP_STATUS_OK) && (status1 == IAP_STATUS_OK))
	{
		*record = (IAP_SequenceNewer(slot1.sequence, slot0.sequence) != 0U) ? slot1 : slot0;
		return IAP_STATUS_OK;
	}
	if (status0 == IAP_STATUS_OK)
	{
		*record = slot0;
		return IAP_STATUS_OK;
	}
	if (status1 == IAP_STATUS_OK)
	{
		*record = slot1;
		return IAP_STATUS_OK;
	}

	IAP_RecordDefaults(record);
	return ((status0 == IAP_STATUS_EEPROM_FAILED) && (status1 == IAP_STATUS_EEPROM_FAILED)) ?
	       IAP_STATUS_EEPROM_FAILED : IAP_STATUS_RECORD_NOT_FOUND;
}

IAP_Status_t IAP_RecordSave(IAP_Record_t *record)
{
	IAP_Record_t current;
	IAP_Record_t verify;
	IAP_Status_t load_status;
	u8 target_address = IAP_EEPROM_SLOT0_ADDRESS;

	if (record == NULL)
	{
		return IAP_STATUS_INVALID_ARGUMENT;
	}

	load_status = IAP_RecordLoad(&current);
	if (load_status == IAP_STATUS_OK)
	{
		record->sequence = current.sequence + 1U;
		if (record->sequence == 0U)
		{
			record->sequence = 1U;
		}

		if (IAP_RecordLoadSlot(IAP_EEPROM_SLOT0_ADDRESS, &verify) == IAP_STATUS_OK &&
		    verify.sequence == current.sequence)
		{
			target_address = IAP_EEPROM_SLOT1_ADDRESS;
		}
	}
	else
	{
		record->sequence = 1U;
	}

	record->magic = IAP_RECORD_MAGIC;
	record->version = IAP_RECORD_VERSION;
	record->record_size = IAP_RECORD_SIZE;
	record->reserved = 0U;
	record->checksum = IAP_RecordChecksum(record);

	if (AT24C02_Write(target_address, (const u8 *)record, (u16)sizeof(*record)) != AT24C02_OK)
	{
		return IAP_STATUS_EEPROM_FAILED;
	}
	if (IAP_RecordLoadSlot(target_address, &verify) != IAP_STATUS_OK ||
	    memcmp(record, &verify, sizeof(*record)) != 0)
	{
		return IAP_STATUS_EEPROM_FAILED;
	}

	return IAP_STATUS_OK;
}

static u8 IAP_VectorValid(const u8 *header)
{
	u32 msp;
	u32 reset_handler;

	memcpy(&msp, header, sizeof(msp));
	memcpy(&reset_handler, header + sizeof(msp), sizeof(reset_handler));

	if (!(((msp >= 0x20000000UL) && (msp <= 0x20020000UL)) ||
	      ((msp >= 0x10000000UL) && (msp <= 0x10010000UL))))
	{
		return 0U;
	}

	return (((reset_handler & 1U) != 0U) &&
	        (reset_handler >= IAP_APP_START_ADDRESS) &&
	        (reset_handler < IAP_FLASH_END_ADDRESS)) ? 1U : 0U;
}

static IAP_Status_t IAP_W25Q64Crc(u32 address, u32 length, u8 *buffer, u32 *crc_out,
	IAP_ProgressCallback_t progress_cb, void *context)
{
	u32 remaining = length;
	u32 processed = 0U;
	u32 crc = IAP_CRC_INITIAL;

	while (remaining > 0U)
	{
		u32 chunk = (remaining > IAP_TRANSFER_BUFFER_SIZE) ? IAP_TRANSFER_BUFFER_SIZE : remaining;
		if (W25Q64_ReadData(address, buffer, chunk) != W25Q64_OK)
		{
			return IAP_STATUS_W25Q64_READ_FAILED;
		}
		crc = IAP_Crc32Update(crc, buffer, chunk);
		address += chunk;
		remaining -= chunk;
		processed += chunk;
		IAP_ReportProgress(progress_cb, context, (u8)(75U + ((processed * 24U) / length)));
	}

	*crc_out = crc ^ IAP_CRC_XOR_OUT;
	return IAP_STATUS_OK;
}

static void IAP_ReportProgress(IAP_ProgressCallback_t progress_cb, void *context, u8 percent)
{
	if (progress_cb != NULL)
	{
		progress_cb(percent, context);
	}
}

IAP_Status_t IAP_PrepareUpdate(IAP_ProgressCallback_t progress_cb, void *context)
{
	FIL *file = NULL;
	FILINFO file_info;
	IAP_Record_t record;
	IAP_Status_t status = IAP_STATUS_OK;
	u8 *buffer = NULL;
	u32 file_size;
	u32 address;
	u32 remaining;
	u32 processed;
	u32 source_crc = IAP_CRC_INITIAL;
	u32 staging_crc;
	u32 jedec_id;
	u32 block;
	UINT bytes_read;
	FRESULT fr;
	u8 file_open = 0U;

	IAP_ReportProgress(progress_cb, context, 0U);
	fr = f_stat(IAP_UPDATE_FILE_PATH, &file_info);
	if (fr != FR_OK)
	{
		return IAP_STATUS_FILE_NOT_FOUND;
	}

	file_size = (u32)file_info.fsize;
	if ((file_size < 8U) || (file_size > IAP_APP_MAX_SIZE) ||
	    ((FSIZE_t)file_size != file_info.fsize))
	{
		return IAP_STATUS_FILE_SIZE_INVALID;
	}

	/* FIL owns a sector cache that SDIO DMA may write for partial-sector reads. */
	file = (FIL *)sys_malloc(SRAMEX, sizeof(FIL));
	if (file == NULL)
	{
		return IAP_STATUS_OUT_OF_MEMORY;
	}
	memset(file, 0, sizeof(*file));

	buffer = (u8 *)sys_malloc(SRAMEX, IAP_TRANSFER_BUFFER_SIZE);
	if (buffer == NULL)
	{
		status = IAP_STATUS_OUT_OF_MEMORY;
		goto cleanup;
	}

	fr = f_open(file, IAP_UPDATE_FILE_PATH, FA_READ);
	if (fr != FR_OK)
	{
		status = IAP_STATUS_FILE_IO;
		goto cleanup;
	}
	file_open = 1U;

	fr = f_read(file, buffer, IAP_TRANSFER_BUFFER_SIZE, &bytes_read);
	if ((fr != FR_OK) || (bytes_read < 8U))
	{
		status = IAP_STATUS_FILE_IO;
		goto cleanup;
	}
	if (IAP_VectorValid(buffer) == 0U)
	{
		status = IAP_STATUS_IMAGE_INVALID;
		goto cleanup;
	}
	if (f_lseek(file, 0U) != FR_OK)
	{
		status = IAP_STATUS_FILE_IO;
		goto cleanup;
	}

	if ((W25Q64_Init() != W25Q64_OK) ||
	    (W25Q64_ReadJEDECID(&jedec_id) != W25Q64_OK) ||
	    (jedec_id != ((((u32)W25Q64_JEDEC_MID) << 16U) | W25Q64_JEDEC_DID)))
	{
		status = IAP_STATUS_W25Q64_NOT_FOUND;
		goto cleanup;
	}

	for (block = 0U; block < (IAP_STAGING_PARTITION_SIZE / IAP_W25Q64_BLOCK_SIZE); block++)
	{
		if (W25Q64_BlockErase64K(IAP_STAGING_ADDRESS + block * IAP_W25Q64_BLOCK_SIZE) != W25Q64_OK)
		{
			status = IAP_STATUS_W25Q64_ERASE_FAILED;
			goto cleanup;
		}
		IAP_ReportProgress(progress_cb, context,
			(u8)(5U + ((block + 1U) * 25U) / (IAP_STAGING_PARTITION_SIZE / IAP_W25Q64_BLOCK_SIZE)));
	}

	address = IAP_STAGING_ADDRESS;
	remaining = file_size;
	processed = 0U;
	while (remaining > 0U)
	{
		u32 chunk = (remaining > IAP_TRANSFER_BUFFER_SIZE) ? IAP_TRANSFER_BUFFER_SIZE : remaining;
		fr = f_read(file, buffer, (UINT)chunk, &bytes_read);
		if ((fr != FR_OK) || (bytes_read != (UINT)chunk))
		{
			status = IAP_STATUS_FILE_IO;
			goto cleanup;
		}
		source_crc = IAP_Crc32Update(source_crc, buffer, chunk);
		if (W25Q64_WriteData(address, buffer, chunk) != W25Q64_OK)
		{
			status = IAP_STATUS_W25Q64_WRITE_FAILED;
			goto cleanup;
		}
		address += chunk;
		remaining -= chunk;
		processed += chunk;
		IAP_ReportProgress(progress_cb, context, (u8)(30U + ((processed * 45U) / file_size)));
	}

	source_crc ^= IAP_CRC_XOR_OUT;
	status = IAP_W25Q64Crc(IAP_STAGING_ADDRESS, file_size, buffer, &staging_crc,
		progress_cb, context);
	if (status != IAP_STATUS_OK)
	{
		goto cleanup;
	}
	if (staging_crc != source_crc)
	{
		status = IAP_STATUS_CRC_MISMATCH;
		goto cleanup;
	}

	status = IAP_RecordLoad(&record);
	if (status == IAP_STATUS_RECORD_NOT_FOUND)
	{
		IAP_RecordDefaults(&record);
	}
	else if (status != IAP_STATUS_OK)
	{
		goto cleanup;
	}

	record.staging_size = file_size;
	record.staging_crc32 = staging_crc;
	record.update_state = IAP_UPDATE_READY;
	record.restore_state = IAP_RESTORE_IDLE;
	status = IAP_RecordSave(&record);
	if (status == IAP_STATUS_OK)
	{
		IAP_ReportProgress(progress_cb, context, 100U);
	}

cleanup:
	if (file_open != 0U)
	{
		(void)f_close(file);
	}
	if (buffer != NULL)
	{
		sys_free(SRAMEX, buffer);
	}
	if (file != NULL)
	{
		sys_free(SRAMEX, file);
	}

	return status;
}

const char *IAP_StatusString(IAP_Status_t status)
{
	switch (status)
	{
		case IAP_STATUS_OK: return "Update ready";
		case IAP_STATUS_FILE_NOT_FOUND: return "update.bin not found";
		case IAP_STATUS_FILE_SIZE_INVALID: return "Invalid update size";
		case IAP_STATUS_FILE_IO: return "Update file read failed";
		case IAP_STATUS_IMAGE_INVALID: return "Invalid firmware image";
		case IAP_STATUS_OUT_OF_MEMORY: return "Update memory unavailable";
		case IAP_STATUS_W25Q64_NOT_FOUND: return "Firmware flash not found";
		case IAP_STATUS_W25Q64_ERASE_FAILED: return "Firmware flash erase failed";
		case IAP_STATUS_W25Q64_WRITE_FAILED: return "Firmware flash write failed";
		case IAP_STATUS_W25Q64_READ_FAILED: return "Firmware flash read failed";
		case IAP_STATUS_CRC_MISMATCH: return "Firmware CRC mismatch";
		case IAP_STATUS_EEPROM_FAILED: return "Update state save failed";
		case IAP_STATUS_RECORD_NOT_FOUND: return "No update state";
		default: return "Update failed";
	}
}
