#ifndef IAP_PROTOCOL_H
#define IAP_PROTOCOL_H

/* Internal Flash layout. */
#define IAP_BOOTLOADER_START_ADDRESS       0x08000000UL
#define IAP_BOOTLOADER_SIZE                0x00010000UL
#define IAP_APP_START_ADDRESS              0x08010000UL
#define IAP_APP_MAX_SIZE                   0x000F0000UL
#define IAP_FLASH_END_ADDRESS              0x08100000UL

/* W25Q64 layout. */
#define IAP_STAGING_ADDRESS                0x00600000UL
#define IAP_STAGING_PARTITION_SIZE         0x00100000UL
#define IAP_BACKUP_ADDRESS                 0x00700000UL
#define IAP_BACKUP_PARTITION_SIZE          0x00100000UL
/* Non-IAP settings storage mirrored to W25Q64. */
#define W25Q64_SETTINGS_ADDRESS            0x00580000UL
#define W25Q64_SETTINGS_SIZE               0x00080000UL
#define W25Q64_SETTINGS_SLOT_SIZE          0x00001000UL
#define W25Q64_SETTINGS_SLOT0_ADDRESS      W25Q64_SETTINGS_ADDRESS
#define W25Q64_SETTINGS_SLOT1_ADDRESS      (W25Q64_SETTINGS_ADDRESS + W25Q64_SETTINGS_SLOT_SIZE)

#if ((W25Q64_SETTINGS_ADDRESS + W25Q64_SETTINGS_SIZE) > IAP_STAGING_ADDRESS)
#error "W25Q64 settings partition overlaps IAP staging partition"
#endif

/* Two independently checksummed EEPROM slots make state changes power-fail safe. */
#define IAP_EEPROM_SLOT0_ADDRESS           128U
#define IAP_EEPROM_SLOT1_ADDRESS           168U
#define IAP_RECORD_MAGIC                   0x424C4932UL /* "BLI2" */
#define IAP_RECORD_VERSION                 2U
#define IAP_RECORD_SIZE                    36U

#define IAP_UPDATE_IDLE                    0x00U
#define IAP_UPDATE_READY                   0xAAU
#define IAP_UPDATE_PROGRAMMING             0x55U
#define IAP_RESTORE_IDLE                   0x00U
#define IAP_RESTORE_REQUESTED              0xBBU
#define IAP_BACKUP_INVALID                 0x00U
#define IAP_BACKUP_VALID                   0xCCU

#define IAP_UPDATE_FILE_PATH               "0:/update.bin"

/* Both the APP and BootLoader declare their local record type with these fields. */
#define IAP_RECORD_MEMBERS                 \
	u32 magic;                               \
	u16 version;                             \
	u16 record_size;                         \
	u32 sequence;                            \
	u32 staging_size;                        \
	u32 staging_crc32;                       \
	u32 backup_size;                         \
	u32 backup_crc32;                        \
	u8 update_state;                         \
	u8 restore_state;                        \
	u8 backup_state;                         \
	u8 reserved;                             \
	u32 checksum

#endif
