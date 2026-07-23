#include "boot_storage.h"

#include "boot_ui.h"

#include <string.h>

#define BOOT_W25_FLASH_SIZE                 0x00800000UL
#define BOOT_W25_PAGE_SIZE                  256U
#define BOOT_W25_JEDEC_ID                   0xEF4017UL
#define BOOT_W25_CMD_WRITE_ENABLE           0x06U
#define BOOT_W25_CMD_READ_STATUS            0x05U
#define BOOT_W25_CMD_READ                   0x03U
#define BOOT_W25_CMD_PAGE_PROGRAM           0x02U
#define BOOT_W25_CMD_ERASE_64K              0xD8U
#define BOOT_W25_CMD_JEDEC_ID               0x9FU
#define BOOT_W25_BUSY_BIT                   0x01U

#define BOOT_AT24_DEVICE_WRITE              0xA0U
#define BOOT_AT24_DEVICE_READ               0xA1U
#define BOOT_AT24_PAGE_SIZE                 8U
#define BOOT_AT24_SIZE                      256U

#define BOOT_W25_CS_HIGH()                  HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, GPIO_PIN_SET)
#define BOOT_W25_CS_LOW()                   HAL_GPIO_WritePin(GPIOE, GPIO_PIN_6, GPIO_PIN_RESET)
#define BOOT_I2C_SCL_HIGH()                 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_SET)
#define BOOT_I2C_SCL_LOW()                  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_8, GPIO_PIN_RESET)
#define BOOT_I2C_SDA_HIGH()                 HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_SET)
#define BOOT_I2C_SDA_LOW()                  HAL_GPIO_WritePin(GPIOB, GPIO_PIN_9, GPIO_PIN_RESET)
#define BOOT_I2C_SDA_READ()                 HAL_GPIO_ReadPin(GPIOB, GPIO_PIN_9)

static void Boot_StorageGpioInit(void);
static u8 Boot_W25_RangeValid(u32 address, u32 length);
static u8 Boot_W25_CommandAddress(u8 command, u32 address);
static u8 Boot_W25_WriteEnable(void);
static u8 Boot_W25_WaitReady(u32 timeout_ms);
static void Boot_I2cDelay(void);
static void Boot_I2cStart(void);
static void Boot_I2cStop(void);
static u8 Boot_I2cWriteByte(u8 value);
static u8 Boot_I2cReadByte(u8 acknowledge);
static u8 Boot_At24Ready(void);
static u8 Boot_At24WritePage(u8 address, const u8 *data, u8 length);
static u8 Boot_At24Write(u8 address, const u8 *data, u16 length);
static u8 Boot_At24Read(u8 address, u8 *data, u16 length);
static u32 Boot_RecordChecksum(const Boot_Record_t *record);
static u8 Boot_RecordValid(const Boot_Record_t *record);
static Boot_RecordStatus_t Boot_RecordLoadSlot(u8 address, Boot_Record_t *record);
static u8 Boot_SequenceNewer(u32 lhs, u32 rhs);

static void Boot_StorageGpioInit(void)
{
	GPIO_InitTypeDef gpio = {0};

	__HAL_RCC_GPIOB_CLK_ENABLE();
	__HAL_RCC_GPIOE_CLK_ENABLE();

	BOOT_W25_CS_HIGH();
	gpio.Pin = GPIO_PIN_6;
	gpio.Mode = GPIO_MODE_OUTPUT_PP;
	gpio.Pull = GPIO_NOPULL;
	gpio.Speed = GPIO_SPEED_FREQ_VERY_HIGH;
	HAL_GPIO_Init(GPIOE, &gpio);
	BOOT_W25_CS_HIGH();

	BOOT_I2C_SCL_HIGH();
	BOOT_I2C_SDA_HIGH();
	gpio.Pin = GPIO_PIN_8 | GPIO_PIN_9;
	gpio.Mode = GPIO_MODE_OUTPUT_OD;
	gpio.Pull = GPIO_PULLUP;
	gpio.Speed = GPIO_SPEED_FREQ_HIGH;
	HAL_GPIO_Init(GPIOB, &gpio);
	BOOT_I2C_SCL_HIGH();
	BOOT_I2C_SDA_HIGH();
}

static u8 Boot_W25_RangeValid(u32 address, u32 length)
{
	return ((address < BOOT_W25_FLASH_SIZE) &&
	        (length <= (BOOT_W25_FLASH_SIZE - address))) ? 1U : 0U;
}

static u8 Boot_W25_CommandAddress(u8 command, u32 address)
{
	u8 header[4];

	header[0] = command;
	header[1] = (u8)(address >> 16U);
	header[2] = (u8)(address >> 8U);
	header[3] = (u8)address;
	return (HAL_SPI_Transmit(&hspi2, header, sizeof(header), 100U) == HAL_OK) ? 1U : 0U;
}

static u8 Boot_W25_WriteEnable(void)
{
	u8 command = BOOT_W25_CMD_WRITE_ENABLE;
	HAL_StatusTypeDef status;

	BOOT_W25_CS_LOW();
	status = HAL_SPI_Transmit(&hspi2, &command, 1U, 100U);
	BOOT_W25_CS_HIGH();
	return (status == HAL_OK) ? 1U : 0U;
}

static u8 Boot_W25_WaitReady(u32 timeout_ms)
{
	u32 start = Boot_TickMs();
	u8 tx[2] = {BOOT_W25_CMD_READ_STATUS, 0xFFU};
	u8 rx[2];

	do
	{
		BOOT_W25_CS_LOW();
		if (HAL_SPI_TransmitReceive(&hspi2, tx, rx, sizeof(tx), 100U) != HAL_OK)
		{
			BOOT_W25_CS_HIGH();
			return 0U;
		}
		BOOT_W25_CS_HIGH();
		if ((rx[1] & BOOT_W25_BUSY_BIT) == 0U)
		{
			return 1U;
		}
		Boot_DelayMs(1U);
	} while ((Boot_TickMs() - start) <= timeout_ms);

	return 0U;
}

u8 Boot_W25_Init(void)
{
	u8 tx[4] = {BOOT_W25_CMD_JEDEC_ID, 0xFFU, 0xFFU, 0xFFU};
	u8 rx[4];
	u32 id;

	Boot_StorageGpioInit();
	BOOT_W25_CS_LOW();
	if (HAL_SPI_TransmitReceive(&hspi2, tx, rx, sizeof(tx), 100U) != HAL_OK)
	{
		BOOT_W25_CS_HIGH();
		return 0U;
	}
	BOOT_W25_CS_HIGH();
	id = ((u32)rx[1] << 16U) | ((u32)rx[2] << 8U) | rx[3];
	return (id == BOOT_W25_JEDEC_ID) ? 1U : 0U;
}

u8 Boot_W25_Read(u32 address, u8 *data, u32 length)
{
	u8 dummy[64];

	if (((data == NULL) && (length != 0U)) || (Boot_W25_RangeValid(address, length) == 0U))
	{
		return 0U;
	}
	memset(dummy, 0xFF, sizeof(dummy));
	BOOT_W25_CS_LOW();
	if (Boot_W25_CommandAddress(BOOT_W25_CMD_READ, address) == 0U)
	{
		BOOT_W25_CS_HIGH();
		return 0U;
	}
	while (length > 0U)
	{
		u16 chunk = (length > sizeof(dummy)) ? (u16)sizeof(dummy) : (u16)length;
		if (HAL_SPI_TransmitReceive(&hspi2, dummy, data, chunk, 100U) != HAL_OK)
		{
			BOOT_W25_CS_HIGH();
			return 0U;
		}
		data += chunk;
		length -= chunk;
	}
	BOOT_W25_CS_HIGH();
	return 1U;
}

u8 Boot_W25_Write(u32 address, u8 *data, u32 length)
{
	if (((data == NULL) && (length != 0U)) || (Boot_W25_RangeValid(address, length) == 0U))
	{
		return 0U;
	}

	while (length > 0U)
	{
		u32 page_left = BOOT_W25_PAGE_SIZE - (address & (BOOT_W25_PAGE_SIZE - 1U));
		u16 chunk = (length > page_left) ? (u16)page_left : (u16)length;

		if ((Boot_W25_WaitReady(1000U) == 0U) || (Boot_W25_WriteEnable() == 0U))
		{
			return 0U;
		}
		BOOT_W25_CS_LOW();
		if ((Boot_W25_CommandAddress(BOOT_W25_CMD_PAGE_PROGRAM, address) == 0U) ||
		    (HAL_SPI_Transmit(&hspi2, data, chunk, 1000U) != HAL_OK))
		{
			BOOT_W25_CS_HIGH();
			return 0U;
		}
		BOOT_W25_CS_HIGH();
		if (Boot_W25_WaitReady(1000U) == 0U)
		{
			return 0U;
		}
		address += chunk;
		data += chunk;
		length -= chunk;
	}
	return 1U;
}

u8 Boot_W25_Erase64K(u32 address)
{
	u8 status;

	if ((address & 0xFFFFU) != 0U || Boot_W25_RangeValid(address, 0x10000U) == 0U)
	{
		return 0U;
	}
	if ((Boot_W25_WaitReady(5000U) == 0U) || (Boot_W25_WriteEnable() == 0U))
	{
		return 0U;
	}
	BOOT_W25_CS_LOW();
	status = Boot_W25_CommandAddress(BOOT_W25_CMD_ERASE_64K, address);
	BOOT_W25_CS_HIGH();
	return ((status != 0U) && (Boot_W25_WaitReady(10000U) != 0U)) ? 1U : 0U;
}

static void Boot_I2cDelay(void)
{
	volatile u32 count;
	for (count = 0U; count < 180U; count++)
	{
		__NOP();
	}
}

static void Boot_I2cStart(void)
{
	BOOT_I2C_SDA_HIGH();
	BOOT_I2C_SCL_HIGH();
	Boot_I2cDelay();
	BOOT_I2C_SDA_LOW();
	Boot_I2cDelay();
	BOOT_I2C_SCL_LOW();
}

static void Boot_I2cStop(void)
{
	BOOT_I2C_SCL_LOW();
	BOOT_I2C_SDA_LOW();
	Boot_I2cDelay();
	BOOT_I2C_SCL_HIGH();
	Boot_I2cDelay();
	BOOT_I2C_SDA_HIGH();
	Boot_I2cDelay();
}

static u8 Boot_I2cWriteByte(u8 value)
{
	u8 bit;
	u16 timeout = 500U;

	for (bit = 0U; bit < 8U; bit++)
	{
		BOOT_I2C_SCL_LOW();
		if ((value & 0x80U) != 0U)
		{
			BOOT_I2C_SDA_HIGH();
		}
		else
		{
			BOOT_I2C_SDA_LOW();
		}
		Boot_I2cDelay();
		BOOT_I2C_SCL_HIGH();
		Boot_I2cDelay();
		value <<= 1U;
	}
	BOOT_I2C_SCL_LOW();
	BOOT_I2C_SDA_HIGH();
	Boot_I2cDelay();
	BOOT_I2C_SCL_HIGH();
	Boot_I2cDelay();
	while ((BOOT_I2C_SDA_READ() != GPIO_PIN_RESET) && (timeout > 0U))
	{
		timeout--;
	}
	BOOT_I2C_SCL_LOW();
	return (timeout > 0U) ? 1U : 0U;
}

static u8 Boot_I2cReadByte(u8 acknowledge)
{
	u8 bit;
	u8 value = 0U;

	BOOT_I2C_SDA_HIGH();
	for (bit = 0U; bit < 8U; bit++)
	{
		BOOT_I2C_SCL_LOW();
		Boot_I2cDelay();
		BOOT_I2C_SCL_HIGH();
		Boot_I2cDelay();
		value <<= 1U;
		if (BOOT_I2C_SDA_READ() != GPIO_PIN_RESET)
		{
			value |= 1U;
		}
	}
	BOOT_I2C_SCL_LOW();
	if (acknowledge != 0U)
	{
		BOOT_I2C_SDA_LOW();
	}
	else
	{
		BOOT_I2C_SDA_HIGH();
	}
	Boot_I2cDelay();
	BOOT_I2C_SCL_HIGH();
	Boot_I2cDelay();
	BOOT_I2C_SCL_LOW();
	BOOT_I2C_SDA_HIGH();
	return value;
}

static u8 Boot_At24Ready(void)
{
	u32 start = Boot_TickMs();

	do
	{
		Boot_I2cStart();
		if (Boot_I2cWriteByte(BOOT_AT24_DEVICE_WRITE) != 0U)
		{
			Boot_I2cStop();
			return 1U;
		}
		Boot_I2cStop();
		Boot_DelayMs(1U);
	} while ((Boot_TickMs() - start) <= 25U);
	return 0U;
}

static u8 Boot_At24WritePage(u8 address, const u8 *data, u8 length)
{
	u8 i;

	Boot_I2cStart();
	if ((Boot_I2cWriteByte(BOOT_AT24_DEVICE_WRITE) == 0U) ||
	    (Boot_I2cWriteByte(address) == 0U))
	{
		Boot_I2cStop();
		return 0U;
	}
	for (i = 0U; i < length; i++)
	{
		if (Boot_I2cWriteByte(data[i]) == 0U)
		{
			Boot_I2cStop();
			return 0U;
		}
	}
	Boot_I2cStop();
	return Boot_At24Ready();
}

static u8 Boot_At24Write(u8 address, const u8 *data, u16 length)
{
	u16 offset = address;

	if (((data == NULL) && (length != 0U)) || ((offset + length) > BOOT_AT24_SIZE))
	{
		return 0U;
	}
	while (length > 0U)
	{
		u8 page_left = (u8)(BOOT_AT24_PAGE_SIZE - (offset % BOOT_AT24_PAGE_SIZE));
		u8 chunk = (length > page_left) ? page_left : (u8)length;
		if (Boot_At24WritePage((u8)offset, data, chunk) == 0U)
		{
			return 0U;
		}
		offset += chunk;
		data += chunk;
		length -= chunk;
	}
	return 1U;
}

static u8 Boot_At24Read(u8 address, u8 *data, u16 length)
{
	u16 i;

	if (((data == NULL) && (length != 0U)) || (((u16)address + length) > BOOT_AT24_SIZE))
	{
		return 0U;
	}
	Boot_I2cStart();
	if ((Boot_I2cWriteByte(BOOT_AT24_DEVICE_WRITE) == 0U) ||
	    (Boot_I2cWriteByte(address) == 0U))
	{
		Boot_I2cStop();
		return 0U;
	}
	Boot_I2cStart();
	if (Boot_I2cWriteByte(BOOT_AT24_DEVICE_READ) == 0U)
	{
		Boot_I2cStop();
		return 0U;
	}
	for (i = 0U; i < length; i++)
	{
		data[i] = Boot_I2cReadByte(((i + 1U) < length) ? 1U : 0U);
	}
	Boot_I2cStop();
	return 1U;
}

static u32 Boot_RecordChecksum(const Boot_Record_t *record)
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

static u8 Boot_RecordValid(const Boot_Record_t *record)
{
	if ((record->magic != IAP_RECORD_MAGIC) ||
	    (record->version != IAP_RECORD_VERSION) ||
	    (record->record_size != IAP_RECORD_SIZE) ||
	    (record->checksum != Boot_RecordChecksum(record)) ||
	    (record->staging_size > IAP_APP_MAX_SIZE))
	{
		return 0U;
	}
	if ((record->backup_state == IAP_BACKUP_VALID) &&
	    ((record->backup_size == 0U) || (record->backup_size > IAP_APP_MAX_SIZE)))
	{
		return 0U;
	}
	return 1U;
}

void Boot_RecordDefaults(Boot_Record_t *record)
{
	memset(record, 0, sizeof(*record));
	record->magic = IAP_RECORD_MAGIC;
	record->version = IAP_RECORD_VERSION;
	record->record_size = IAP_RECORD_SIZE;
}

static Boot_RecordStatus_t Boot_RecordLoadSlot(u8 address, Boot_Record_t *record)
{
	if (Boot_At24Read(address, (u8 *)record, (u16)sizeof(*record)) == 0U)
	{
		return BOOT_RECORD_IO_ERROR;
	}
	return (Boot_RecordValid(record) != 0U) ? BOOT_RECORD_OK : BOOT_RECORD_EMPTY;
}

static u8 Boot_SequenceNewer(u32 lhs, u32 rhs)
{
	return (((s32)(lhs - rhs)) > 0) ? 1U : 0U;
}

Boot_RecordStatus_t Boot_RecordLoad(Boot_Record_t *record)
{
	Boot_Record_t slot0;
	Boot_Record_t slot1;
	Boot_RecordStatus_t status0;
	Boot_RecordStatus_t status1;

	if (record == NULL)
	{
		return BOOT_RECORD_IO_ERROR;
	}
	Boot_StorageGpioInit();
	status0 = Boot_RecordLoadSlot(IAP_EEPROM_SLOT0_ADDRESS, &slot0);
	status1 = Boot_RecordLoadSlot(IAP_EEPROM_SLOT1_ADDRESS, &slot1);
	if ((status0 == BOOT_RECORD_OK) && (status1 == BOOT_RECORD_OK))
	{
		*record = (Boot_SequenceNewer(slot1.sequence, slot0.sequence) != 0U) ? slot1 : slot0;
		return BOOT_RECORD_OK;
	}
	if (status0 == BOOT_RECORD_OK)
	{
		*record = slot0;
		return BOOT_RECORD_OK;
	}
	if (status1 == BOOT_RECORD_OK)
	{
		*record = slot1;
		return BOOT_RECORD_OK;
	}
	Boot_RecordDefaults(record);
	return ((status0 == BOOT_RECORD_IO_ERROR) && (status1 == BOOT_RECORD_IO_ERROR)) ?
	       BOOT_RECORD_IO_ERROR : BOOT_RECORD_EMPTY;
}

Boot_RecordStatus_t Boot_RecordSave(Boot_Record_t *record)
{
	Boot_Record_t current;
	Boot_Record_t verify;
	Boot_RecordStatus_t status;
	u8 target = IAP_EEPROM_SLOT0_ADDRESS;

	if (record == NULL)
	{
		return BOOT_RECORD_IO_ERROR;
	}
	status = Boot_RecordLoad(&current);
	if (status == BOOT_RECORD_OK)
	{
		record->sequence = current.sequence + 1U;
		if (record->sequence == 0U)
		{
			record->sequence = 1U;
		}
		if ((Boot_RecordLoadSlot(IAP_EEPROM_SLOT0_ADDRESS, &verify) == BOOT_RECORD_OK) &&
		    (verify.sequence == current.sequence))
		{
			target = IAP_EEPROM_SLOT1_ADDRESS;
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
	record->checksum = Boot_RecordChecksum(record);
	if ((Boot_At24Write(target, (const u8 *)record, (u16)sizeof(*record)) == 0U) ||
	    (Boot_RecordLoadSlot(target, &verify) != BOOT_RECORD_OK) ||
	    (memcmp(record, &verify, sizeof(*record)) != 0))
	{
		return BOOT_RECORD_IO_ERROR;
	}
	return BOOT_RECORD_OK;
}
