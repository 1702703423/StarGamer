#include "bootloader.h"

#include "boot_crc.h"
#include "boot_storage.h"
#include "boot_ui.h"

#include <string.h>

#define BOOT_TRANSFER_BUFFER_SIZE          1024U
#define BOOT_W25_BLOCK_SIZE                0x00010000UL
#define BOOT_FLASH_SECTOR_COUNT            8U

static u8 s_buffer[BOOT_TRANSFER_BUFFER_SIZE];
static u8 s_ui_ready;
static u8 s_last_progress = 0xFFU;

static const u32 s_app_sectors[BOOT_FLASH_SECTOR_COUNT] =
{
	FLASH_SECTOR_4, FLASH_SECTOR_5, FLASH_SECTOR_6, FLASH_SECTOR_7,
	FLASH_SECTOR_8, FLASH_SECTOR_9, FLASH_SECTOR_10, FLASH_SECTOR_11
};

static void Boot_EnsureUi(void);
static void Boot_ShowProgress(const char *title, const char *detail, u32 done, u32 total);
static u8 Boot_VectorValid(const u8 *header);
static u8 Boot_AppValid(void);
static u8 Boot_W25Crc(u32 address, u32 length, u32 *crc_out, const char *title, const char *detail);
static u8 Boot_W25ErasePartition(u32 address, u32 size, const char *title);
static u8 Boot_BackupApp(Boot_Record_t *record);
static u8 Boot_EraseAppFlash(const char *title);
static u8 Boot_ProgramFromW25(u32 source, u32 length, const char *title);
static u8 Boot_VerifyWithW25(u32 source, u32 length, const char *title);
static void Boot_FlushFlashCaches(void);
static u8 Boot_RestoreBackup(Boot_Record_t *record);
static u8 Boot_UpdateFailure(Boot_Record_t *record, const char *reason);
static u8 Boot_InstallUpdate(Boot_Record_t *record);
static void Boot_Menu(Boot_Record_t *record) __attribute__((noreturn));
static void Boot_JumpToApp(void) __attribute__((noreturn));
static void Boot_Branch(u32 msp, u32 reset_handler) __attribute__((naked, noreturn));

static void Boot_EnsureUi(void)
{
	if (s_ui_ready == 0U)
	{
		Boot_UI_Init();
		s_ui_ready = 1U;
	}
}

static void Boot_ShowProgress(const char *title, const char *detail, u32 done, u32 total)
{
	u8 percent;

	if (total == 0U)
	{
		percent = 0U;
	}
	else
	{
		percent = (u8)((done * 100U) / total);
	}
	if ((percent != s_last_progress) || (percent == 100U))
	{
		Boot_UI_Progress(title, detail, percent);
		s_last_progress = percent;
	}
}

static u8 Boot_VectorValid(const u8 *header)
{
	u32 msp;
	u32 reset_handler;
	u32 reset_address;
	u8 msp_valid;

	memcpy(&msp, header, sizeof(msp));
	memcpy(&reset_handler, header + sizeof(msp), sizeof(reset_handler));
	reset_address = reset_handler & ~1UL;
	msp_valid = (((msp >= 0x20000000UL) && (msp <= 0x20020000UL)) ||
	             ((msp >= 0x10000000UL) && (msp <= 0x10010000UL))) ? 1U : 0U;
	return ((msp_valid != 0U) && ((reset_handler & 1U) != 0U) &&
	        (reset_address >= IAP_APP_START_ADDRESS) &&
	        (reset_address < IAP_FLASH_END_ADDRESS)) ? 1U : 0U;
}

static u8 Boot_AppValid(void)
{
	return Boot_VectorValid((const u8 *)IAP_APP_START_ADDRESS);
}

static u8 Boot_W25Crc(u32 address, u32 length, u32 *crc_out, const char *title, const char *detail)
{
	u32 crc = BOOT_CRC32_INITIAL;
	u32 remaining = length;
	u32 processed = 0U;

	s_last_progress = 0xFFU;
	while (remaining > 0U)
	{
		u32 chunk = (remaining > sizeof(s_buffer)) ? sizeof(s_buffer) : remaining;
		if (Boot_W25_Read(address, s_buffer, chunk) == 0U)
		{
			return 0U;
		}
		crc = Boot_Crc32Update(crc, s_buffer, chunk);
		address += chunk;
		remaining -= chunk;
		processed += chunk;
		Boot_ShowProgress(title, detail, processed, length);
	}
	*crc_out = Boot_Crc32Finish(crc);
	return 1U;
}

static u8 Boot_W25ErasePartition(u32 address, u32 size, const char *title)
{
	u32 block_count = size / BOOT_W25_BLOCK_SIZE;
	u32 block;

	s_last_progress = 0xFFU;
	for (block = 0U; block < block_count; block++)
	{
		if (Boot_W25_Erase64K(address + block * BOOT_W25_BLOCK_SIZE) == 0U)
		{
			return 0U;
		}
		Boot_ShowProgress(title, "ERASING STORAGE", block + 1U, block_count);
	}
	return 1U;
}

static u8 Boot_BackupApp(Boot_Record_t *record)
{
	u32 source = IAP_APP_START_ADDRESS;
	u32 destination = IAP_BACKUP_ADDRESS;
	u32 remaining = IAP_APP_MAX_SIZE;
	u32 processed = 0U;
	u32 source_crc = BOOT_CRC32_INITIAL;
	u32 backup_crc;

	Boot_EnsureUi();
	if ((Boot_AppValid() == 0U) || (Boot_W25_Init() == 0U))
	{
		return 0U;
	}
	if (Boot_W25ErasePartition(IAP_BACKUP_ADDRESS, IAP_BACKUP_PARTITION_SIZE, "BACKUP APP") == 0U)
	{
		return 0U;
	}

	s_last_progress = 0xFFU;
	while (remaining > 0U)
	{
		u32 chunk = (remaining > sizeof(s_buffer)) ? sizeof(s_buffer) : remaining;
		memcpy(s_buffer, (const void *)source, chunk);
		source_crc = Boot_Crc32Update(source_crc, s_buffer, chunk);
		if (Boot_W25_Write(destination, s_buffer, chunk) == 0U)
		{
			return 0U;
		}
		source += chunk;
		destination += chunk;
		remaining -= chunk;
		processed += chunk;
		Boot_ShowProgress("BACKUP APP", "COPYING CURRENT APP", processed, IAP_APP_MAX_SIZE);
	}
	source_crc = Boot_Crc32Finish(source_crc);
	if ((Boot_W25Crc(IAP_BACKUP_ADDRESS, IAP_APP_MAX_SIZE, &backup_crc,
	                 "BACKUP APP", "VERIFYING BACKUP") == 0U) ||
	    (backup_crc != source_crc))
	{
		return 0U;
	}

	record->backup_size = IAP_APP_MAX_SIZE;
	record->backup_crc32 = backup_crc;
	record->backup_state = IAP_BACKUP_VALID;
	return (Boot_RecordSave(record) == BOOT_RECORD_OK) ? 1U : 0U;
}

static u8 Boot_EraseAppFlash(const char *title)
{
	FLASH_EraseInitTypeDef erase = {0};
	u32 sector_error;
	u32 i;
	u8 success = 1U;

	if (HAL_FLASH_Unlock() != HAL_OK)
	{
		return 0U;
	}
	__HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
	                       FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
	erase.TypeErase = FLASH_TYPEERASE_SECTORS;
	erase.NbSectors = 1U;
	erase.VoltageRange = FLASH_VOLTAGE_RANGE_3;
	s_last_progress = 0xFFU;
	for (i = 0U; i < BOOT_FLASH_SECTOR_COUNT; i++)
	{
		erase.Sector = s_app_sectors[i];
		if (HAL_FLASHEx_Erase(&erase, &sector_error) != HAL_OK)
		{
			success = 0U;
			break;
		}
		Boot_ShowProgress(title, "ERASING APP FLASH", i + 1U, BOOT_FLASH_SECTOR_COUNT);
	}
	if (success == 0U)
	{
		(void)HAL_FLASH_Lock();
	}
	return success;
}

static u8 Boot_ProgramFromW25(u32 source, u32 length, const char *title)
{
	u32 destination = IAP_APP_START_ADDRESS;
	u32 remaining = length;
	u32 processed = 0U;

	s_last_progress = 0xFFU;
	while (remaining > 0U)
	{
		u32 chunk = (remaining > sizeof(s_buffer)) ? sizeof(s_buffer) : remaining;
		u32 offset;
		if (Boot_W25_Read(source, s_buffer, chunk) == 0U)
		{
			(void)HAL_FLASH_Lock();
			return 0U;
		}
		for (offset = 0U; offset < chunk; offset += 4U)
		{
			u32 word = 0xFFFFFFFFUL;
			u32 bytes = ((chunk - offset) >= 4U) ? 4U : (chunk - offset);
			memcpy(&word, &s_buffer[offset], bytes);
			if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, destination, word) != HAL_OK)
			{
				(void)HAL_FLASH_Lock();
				return 0U;
			}
			destination += 4U;
		}
		source += chunk;
		remaining -= chunk;
		processed += chunk;
		Boot_ShowProgress(title, "PROGRAMMING APP", processed, length);
	}
	if (HAL_FLASH_Lock() != HAL_OK)
	{
		return 0U;
	}

	/* HAL flushes ART caches after erase, but not after word programming. */
	Boot_FlushFlashCaches();
	return 1U;
}

static u8 Boot_VerifyWithW25(u32 source, u32 length, const char *title)
{
	u32 flash_address = IAP_APP_START_ADDRESS;
	u32 remaining = length;
	u32 processed = 0U;

	s_last_progress = 0xFFU;
	while (remaining > 0U)
	{
		u32 chunk = (remaining > sizeof(s_buffer)) ? sizeof(s_buffer) : remaining;
		if ((Boot_W25_Read(source, s_buffer, chunk) == 0U) ||
		    (memcmp(s_buffer, (const void *)flash_address, chunk) != 0))
		{
			return 0U;
		}
		source += chunk;
		flash_address += chunk;
		remaining -= chunk;
		processed += chunk;
		Boot_ShowProgress(title, "VERIFYING APP", processed, length);
	}
	return 1U;
}

static void Boot_FlushFlashCaches(void)
{
	FLASH_FlushCaches();
	__DSB();
	__ISB();
}

static u8 Boot_RestoreBackup(Boot_Record_t *record)
{
	u32 crc;

	Boot_EnsureUi();
	if ((record->backup_state != IAP_BACKUP_VALID) ||
	    (record->backup_size == 0U) || (record->backup_size > IAP_APP_MAX_SIZE) ||
	    (Boot_W25_Init() == 0U))
	{
		return 0U;
	}
	if ((Boot_W25_Read(IAP_BACKUP_ADDRESS, s_buffer, 8U) == 0U) ||
	    (Boot_VectorValid(s_buffer) == 0U))
	{
		return 0U;
	}
	if ((Boot_W25Crc(IAP_BACKUP_ADDRESS, record->backup_size, &crc,
	                 "RESTORE APP", "CHECKING BACKUP") == 0U) ||
	    (crc != record->backup_crc32))
	{
		return 0U;
	}

	record->restore_state = IAP_RESTORE_REQUESTED;
	if (Boot_RecordSave(record) != BOOT_RECORD_OK)
	{
		return 0U;
	}
	if ((Boot_EraseAppFlash("RESTORE APP") == 0U) ||
	    (Boot_ProgramFromW25(IAP_BACKUP_ADDRESS, record->backup_size, "RESTORE APP") == 0U) ||
	    (Boot_VerifyWithW25(IAP_BACKUP_ADDRESS, record->backup_size, "RESTORE APP") == 0U))
	{
		return 0U;
	}

	record->restore_state = IAP_RESTORE_IDLE;
	record->update_state = IAP_UPDATE_IDLE;
	if (Boot_RecordSave(record) != BOOT_RECORD_OK)
	{
		return 0U;
	}
	Boot_UI_Message("RESTORE APP", "RESTORE COMPLETE", "RESTARTING");
	Boot_DelayMs(400U);
	NVIC_SystemReset();
}

static u8 Boot_UpdateFailure(Boot_Record_t *record, const char *reason)
{
	if ((record->update_state == IAP_UPDATE_PROGRAMMING) &&
	    (record->backup_state == IAP_BACKUP_VALID))
	{
		record->restore_state = IAP_RESTORE_REQUESTED;
		record->update_state = IAP_UPDATE_IDLE;
		if (Boot_RecordSave(record) == BOOT_RECORD_OK)
		{
			Boot_UI_Message("INSTALL UPDATE", reason, "RESTORING BACKUP");
			Boot_DelayMs(500U);
			return Boot_RestoreBackup(record);
		}
		Boot_UI_Message("INSTALL UPDATE", reason, "BACKUP AVAILABLE");
		return 0U;
	}

	record->update_state = IAP_UPDATE_IDLE;
	(void)Boot_RecordSave(record);
	Boot_UI_Message("INSTALL UPDATE", reason, "APP NOT CHANGED");
	return 0U;
}

static u8 Boot_InstallUpdate(Boot_Record_t *record)
{
	u32 staging_crc;

	Boot_EnsureUi();
	if (Boot_W25_Init() == 0U)
	{
		return 0U;
	}
	if ((record->staging_size < 8U) || (record->staging_size > IAP_APP_MAX_SIZE))
	{
		return Boot_UpdateFailure(record, "UPDATE SIZE ERROR");
	}
	if ((Boot_W25_Read(IAP_STAGING_ADDRESS, s_buffer, 8U) == 0U) ||
	    (Boot_VectorValid(s_buffer) == 0U))
	{
		return Boot_UpdateFailure(record, "UPDATE IMAGE INVALID");
	}
	if ((Boot_W25Crc(IAP_STAGING_ADDRESS, record->staging_size, &staging_crc,
	                 "INSTALL UPDATE", "CHECKING UPDATE") == 0U) ||
	    (staging_crc != record->staging_crc32))
	{
		return Boot_UpdateFailure(record, "UPDATE CRC FAILED");
	}

	if (record->update_state == IAP_UPDATE_READY)
	{
		if (Boot_AppValid() != 0U)
		{
			if (Boot_BackupApp(record) == 0U)
			{
				Boot_UI_Message("INSTALL UPDATE", "BACKUP FAILED", "APP NOT CHANGED");
				return 0U;
			}
		}
		else
		{
			record->backup_state = IAP_BACKUP_INVALID;
			record->backup_size = 0U;
			record->backup_crc32 = 0U;
		}
		record->update_state = IAP_UPDATE_PROGRAMMING;
		if (Boot_RecordSave(record) != BOOT_RECORD_OK)
		{
			Boot_UI_Message("INSTALL UPDATE", "STATE SAVE FAILED", "APP NOT CHANGED");
			return 0U;
		}
	}

	if ((Boot_EraseAppFlash("INSTALL UPDATE") == 0U) ||
	    (Boot_ProgramFromW25(IAP_STAGING_ADDRESS, record->staging_size, "INSTALL UPDATE") == 0U) ||
	    (Boot_VerifyWithW25(IAP_STAGING_ADDRESS, record->staging_size, "INSTALL UPDATE") == 0U))
	{
		if (record->backup_state == IAP_BACKUP_VALID)
		{
			record->restore_state = IAP_RESTORE_REQUESTED;
			record->update_state = IAP_UPDATE_IDLE;
			if (Boot_RecordSave(record) == BOOT_RECORD_OK)
			{
				Boot_UI_Message("INSTALL UPDATE", "PROGRAM FAILED", "RESTORING BACKUP");
				Boot_DelayMs(500U);
				return Boot_RestoreBackup(record);
			}
		}
		Boot_UI_Message("INSTALL UPDATE", "PROGRAM FAILED", "USE BOOT MENU");
		return 0U;
	}

	record->update_state = IAP_UPDATE_IDLE;
	record->restore_state = IAP_RESTORE_IDLE;
	if (Boot_RecordSave(record) != BOOT_RECORD_OK)
	{
		Boot_UI_Message("INSTALL UPDATE", "STATE SAVE FAILED", "UPDATE WILL RETRY");
		return 0U;
	}
	Boot_UI_Message("INSTALL UPDATE", "UPDATE COMPLETE", "RESTARTING");
	Boot_DelayMs(400U);
	NVIC_SystemReset();
}

static void Boot_Menu(Boot_Record_t *record)
{
	u8 selected = 0U;
	u32 key;

	Boot_EnsureUi();
	for (;;)
	{
		Boot_UI_Menu(selected);
		key = Boot_KeyWait();
		if ((key & BOOT_KEY_UP) != 0U)
		{
			selected = (selected == 0U) ? 3U : (u8)(selected - 1U);
		}
		else if ((key & BOOT_KEY_DOWN) != 0U)
		{
			selected = (u8)((selected + 1U) % 4U);
		}
		else if ((key & BOOT_KEY_CANCEL) != 0U)
		{
			if (Boot_AppValid() != 0U)
			{
				Boot_JumpToApp();
			}
		}
		else if ((key & BOOT_KEY_OK) != 0U)
		{
			switch (selected)
			{
				case 0U:
					if (Boot_RestoreBackup(record) == 0U)
					{
						Boot_UI_Message("RESTORE APP", "NO VALID BACKUP", "PRESS ANY KEY");
						(void)Boot_KeyWait();
					}
					break;
				case 1U:
					if ((record->update_state != IAP_UPDATE_READY) &&
					    (record->update_state != IAP_UPDATE_PROGRAMMING))
					{
						Boot_UI_Message("INSTALL UPDATE", "NO UPDATE READY", "PRESS ANY KEY");
						(void)Boot_KeyWait();
					}
					else if (Boot_InstallUpdate(record) == 0U)
					{
						(void)Boot_KeyWait();
					}
					break;
				case 2U:
					if (Boot_BackupApp(record) != 0U)
					{
						Boot_UI_Message("BACKUP APP", "BACKUP COMPLETE", "PRESS ANY KEY");
					}
					else
					{
						Boot_UI_Message("BACKUP APP", "BACKUP FAILED", "PRESS ANY KEY");
					}
					(void)Boot_KeyWait();
					break;
				default:
					if (Boot_AppValid() != 0U)
					{
						Boot_JumpToApp();
					}
					Boot_UI_Message("BOOT APP", "APP IS INVALID", "PRESS ANY KEY");
					(void)Boot_KeyWait();
					break;
			}
		}
	}
}

static void Boot_Branch(u32 msp, u32 reset_handler)
{
	__asm volatile(
		"movs r2, #0\n"
		"msr control, r2\n"
		"isb\n"
		"msr msp, r0\n"
		"cpsie i\n"
		"bx r1\n");
}

static void Boot_JumpToApp(void)
{
	Boot_Record_t record;
	u32 msp;
	u32 reset_handler;
	u32 i;

	if (Boot_AppValid() == 0U)
	{
		Boot_EnsureUi();
		Boot_UI_Message("BOOT APP", "APP IS INVALID", "USE BOOT MENU");
		if (Boot_RecordLoad(&record) != BOOT_RECORD_OK)
		{
			Boot_RecordDefaults(&record);
		}
		Boot_Menu(&record);
	}
	__disable_irq();
	SysTick->CTRL = 0U;
	SysTick->LOAD = 0U;
	SysTick->VAL = 0U;
	(void)HAL_TIM_Base_Stop_IT(&htim13);

	for (i = 0U; i < 8U; i++)
	{
		NVIC->ICER[i] = 0xFFFFFFFFUL;
		NVIC->ICPR[i] = 0xFFFFFFFFUL;
	}
	SCB->ICSR = SCB_ICSR_PENDSTCLR_Msk | SCB_ICSR_PENDSVCLR_Msk;
	RCC->AHB1RSTR = 0xFFFFFFFFUL;
	RCC->AHB2RSTR = 0xFFFFFFFFUL;
	RCC->AHB3RSTR = 0xFFFFFFFFUL;
	RCC->APB1RSTR = 0xFFFFFFFFUL;
	RCC->APB2RSTR = 0xFFFFFFFFUL;
	__DSB();
	RCC->AHB1RSTR = 0U;
	RCC->AHB2RSTR = 0U;
	RCC->AHB3RSTR = 0U;
	RCC->APB1RSTR = 0U;
	RCC->APB2RSTR = 0U;

	Boot_FlushFlashCaches();
	msp = *(const u32 *)IAP_APP_START_ADDRESS;
	reset_handler = *(const u32 *)(IAP_APP_START_ADDRESS + 4U);

	__set_BASEPRI(0U);
	__set_FAULTMASK(0U);
	__set_CONTROL(0);
	SCB->VTOR = IAP_APP_START_ADDRESS;
	__DSB();
	__ISB();
	Boot_Branch(msp, reset_handler);
}

void Bootloader_Run(void)
{
	Boot_Record_t record;
	Boot_RecordStatus_t record_status;
	u8 app_valid;
	u8 menu_requested;

	record_status = Boot_RecordLoad(&record);
	app_valid = Boot_AppValid();
	menu_requested = Boot_KeyHeld(BOOT_KEY_CANCEL, 30U);

	if ((record_status == BOOT_RECORD_OK) &&
	    (record.restore_state == IAP_RESTORE_REQUESTED))
	{
		if (Boot_RestoreBackup(&record) == 0U)
		{
			Boot_UI_Message("RESTORE APP", "RESTORE FAILED", "USE BOOT MENU");
		}
	}
	else if ((record_status == BOOT_RECORD_OK) &&
	         ((record.update_state == IAP_UPDATE_READY) ||
	          (record.update_state == IAP_UPDATE_PROGRAMMING)))
	{
		if (Boot_InstallUpdate(&record) == 0U)
		{
			Boot_UI_Message("INSTALL UPDATE", "UPDATE STOPPED", "USE BOOT MENU");
		}
	}
	else if ((menu_requested == 0U) && (app_valid != 0U))
	{
		Boot_JumpToApp();
	}

	if (record_status != BOOT_RECORD_OK)
	{
		Boot_RecordDefaults(&record);
		if ((record_status == BOOT_RECORD_IO_ERROR) && (app_valid == 0U))
		{
			Boot_EnsureUi();
			Boot_UI_Message("BOOTLOADER", "EEPROM NOT READY", "USE BOOT MENU");
			(void)Boot_KeyWait();
		}
	}
	Boot_Menu(&record);
}
