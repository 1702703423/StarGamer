#include "Memory_Dump.h"

#include "Malloc.h"
#include "IAP_Protocol.h"
#include "System_Status.h"
#include "fatfs.h"
#include "main.h"
#include "w25q64.h"

#include <stddef.h>
#include <string.h>

#define MEMORY_DUMP_MAGIC                 0x504D4453UL /* "SDMP" */
#define MEMORY_DUMP_VERSION               1UL
#define MEMORY_DUMP_DATA_OFFSET           256UL
#define MEMORY_DUMP_IO_BUFFER_SIZE        4096UL
#define MEMORY_DUMP_FNV_OFFSET            2166136261UL
#define MEMORY_DUMP_FNV_PRIME             16777619UL
#define MEMORY_DUMP_EXPORT_PENDING        0xFFFFFFFFUL
#define MEMORY_DUMP_EXPORT_COMPLETE       0x00000000UL

#if ((MEMORY_DUMP_FLASH_ADDRESS + MEMORY_DUMP_FLASH_SIZE) != W25Q64_SETTINGS_ADDRESS)
#error "Memory dump partition must end at the system settings partition"
#endif

#if (MEMORY_DUMP_FLASH_ADDRESS < (SYSTEM_FONT_FLASH_ADDR + SYSTEM_FONT_FLASH_SIZE))
#error "Memory dump partition overlaps the font partition"
#endif

#define MEMORY_DUMP_REGION_FLAG_CCM       (1UL << 0)
#define MEMORY_DUMP_REGION_FLAG_MAIN      (1UL << 1)
#define MEMORY_DUMP_REGION_FLAG_SRAMEX    (1UL << 2)

#define MEMORY_DUMP_CCM_BASE              0x10000000UL
#define MEMORY_DUMP_CCM_SIZE              0x00010000UL
#define MEMORY_DUMP_MAIN_BASE             0x20000000UL
#define MEMORY_DUMP_MAIN_SIZE             0x00020000UL
#define MEMORY_DUMP_SRAMEX_BASE           0x68000000UL
#define MEMORY_DUMP_SRAMEX_SIZE           0x00100000UL

#define MEMORY_DUMP_SPI_TIMEOUT           1000000UL
#define MEMORY_DUMP_BUSY_POLL_LIMIT       1500000UL

#define MEMORY_DUMP_GPIO_MODE_MASK(pin)   (3UL << ((pin) * 2U))
#define MEMORY_DUMP_GPIO_MODE_AF(pin)     (2UL << ((pin) * 2U))
#define MEMORY_DUMP_GPIO_MODE_OUT(pin)    (1UL << ((pin) * 2U))
#define MEMORY_DUMP_GPIO_SPEED_HIGH(pin)  (3UL << ((pin) * 2U))
#define MEMORY_DUMP_GPIO_AFR_MASK(pin)    (0xFUL << ((pin) * 4U))
#define MEMORY_DUMP_GPIO_AFR_AF5(pin)     (5UL << ((pin) * 4U))

#define MEMORY_DUMP_FLASH_CS_HIGH()       (GPIOE->BSRR = GPIO_PIN_6)
#define MEMORY_DUMP_FLASH_CS_LOW()        (GPIOE->BSRR = ((u32)GPIO_PIN_6 << 16U))

typedef struct Memory_Dump_RegionRecord
{
	u32 base_address;
	u32 length;
	u32 file_offset;
} Memory_Dump_RegionRecord_t;

typedef struct Memory_Dump_Header
{
	u32 magic;
	u32 version;
	u32 header_size;
	u32 total_size;
	u32 payload_size;
	u32 region_count;
	u32 region_flags;
	u32 cpuid;
	Memory_Dump_FaultRecord_t fault;
	Memory_Dump_RegionRecord_t regions[3];
	u32 payload_checksum;
	u32 header_checksum;
	u32 export_state;
} Memory_Dump_Header_t;

typedef char Memory_Dump_HeaderSizeCheck[
	(sizeof(Memory_Dump_Header_t) <= MEMORY_DUMP_DATA_OFFSET) ? 1 : -1];

static Memory_Dump_Header_t s_memory_dump_header;

static void memory_dump_spi_init(void);
static u8 memory_dump_spi_transfer(u8 transmit, u8* receive);
static void memory_dump_spi_finish(void);
static u8 memory_dump_flash_read_status(u8* status);
static u8 memory_dump_flash_wait_ready(void);
static u8 memory_dump_flash_write_enable(void);
static u8 memory_dump_flash_init(void);
static u8 memory_dump_flash_erase_block(u32 address);
static u8 memory_dump_flash_program(u32 address, const volatile u8* data,
	u16 count, u32* checksum);
static u32 memory_dump_checksum(const u8* data, u32 length);
static u32 memory_dump_checksum_update(u32 checksum, u8 data);
static u8 memory_dump_validate_header(const Memory_Dump_Header_t* header);
static void memory_dump_report(Memory_Dump_ProgressCallback_t progress,
	u8 percent, const char* phase, u8* previous);

static void memory_dump_dma_stop(DMA_Stream_TypeDef* stream)
{
	u32 timeout = MEMORY_DUMP_SPI_TIMEOUT;

	stream->CR &= ~DMA_SxCR_EN;
	while ((stream->CR & DMA_SxCR_EN) != 0U && timeout != 0U)
	{
		timeout--;
	}
}

static void memory_dump_spi_init(void)
{
	u32 mask;

	RCC->AHB1ENR |= RCC_AHB1ENR_GPIOBEN | RCC_AHB1ENR_GPIOCEN |
	                RCC_AHB1ENR_GPIOEEN | RCC_AHB1ENR_DMA1EN;
	RCC->APB1ENR |= RCC_APB1ENR_SPI2EN;
	__DSB();

	SPI2->CR2 &= ~(SPI_CR2_TXDMAEN | SPI_CR2_RXDMAEN | SPI_CR2_TXEIE |
	               SPI_CR2_RXNEIE | SPI_CR2_ERRIE);
	memory_dump_dma_stop(DMA1_Stream4);
	SPI2->CR1 &= ~SPI_CR1_SPE;
	RCC->APB1RSTR |= RCC_APB1RSTR_SPI2RST;
	RCC->APB1RSTR &= ~RCC_APB1RSTR_SPI2RST;

	mask = MEMORY_DUMP_GPIO_MODE_MASK(2U) |
	       MEMORY_DUMP_GPIO_MODE_MASK(3U);
	GPIOC->MODER = (GPIOC->MODER & ~mask) |
	               MEMORY_DUMP_GPIO_MODE_AF(2U) |
	               MEMORY_DUMP_GPIO_MODE_AF(3U);
	GPIOC->OTYPER &= ~(GPIO_PIN_2 | GPIO_PIN_3);
	GPIOC->OSPEEDR |= MEMORY_DUMP_GPIO_SPEED_HIGH(2U) |
	                  MEMORY_DUMP_GPIO_SPEED_HIGH(3U);
	GPIOC->PUPDR &= ~mask;
	mask = MEMORY_DUMP_GPIO_AFR_MASK(2U) |
	       MEMORY_DUMP_GPIO_AFR_MASK(3U);
	GPIOC->AFR[0] = (GPIOC->AFR[0] & ~mask) |
	                MEMORY_DUMP_GPIO_AFR_AF5(2U) |
	                MEMORY_DUMP_GPIO_AFR_AF5(3U);

	mask = MEMORY_DUMP_GPIO_MODE_MASK(13U);
	GPIOB->MODER = (GPIOB->MODER & ~mask) | MEMORY_DUMP_GPIO_MODE_AF(13U);
	GPIOB->OTYPER &= ~GPIO_PIN_13;
	GPIOB->OSPEEDR |= MEMORY_DUMP_GPIO_SPEED_HIGH(13U);
	GPIOB->PUPDR &= ~mask;
	mask = MEMORY_DUMP_GPIO_AFR_MASK(5U);
	GPIOB->AFR[1] = (GPIOB->AFR[1] & ~mask) | MEMORY_DUMP_GPIO_AFR_AF5(5U);

	mask = MEMORY_DUMP_GPIO_MODE_MASK(6U);
	GPIOE->MODER = (GPIOE->MODER & ~mask) | MEMORY_DUMP_GPIO_MODE_OUT(6U);
	GPIOE->OTYPER &= ~GPIO_PIN_6;
	GPIOE->OSPEEDR |= MEMORY_DUMP_GPIO_SPEED_HIGH(6U);
	GPIOE->PUPDR &= ~mask;
	MEMORY_DUMP_FLASH_CS_HIGH();

	SPI2->CR1 = SPI_CR1_MSTR | SPI_CR1_SSM | SPI_CR1_SSI | SPI_CR1_BR_0;
	SPI2->CR2 = 0U;
	SPI2->CR1 |= SPI_CR1_SPE;
}

static u8 memory_dump_spi_transfer(u8 transmit, u8* receive)
{
	u32 timeout = MEMORY_DUMP_SPI_TIMEOUT;
	u8 value;

	while ((SPI2->SR & SPI_SR_TXE) == 0U && timeout != 0U)
	{
		timeout--;
	}
	if (timeout == 0U)
	{
		return 0U;
	}

	*(__IO u8*)&SPI2->DR = transmit;
	timeout = MEMORY_DUMP_SPI_TIMEOUT;
	while ((SPI2->SR & SPI_SR_RXNE) == 0U && timeout != 0U)
	{
		timeout--;
	}
	if (timeout == 0U)
	{
		return 0U;
	}

	value = *(__IO u8*)&SPI2->DR;
	if (receive != NULL)
	{
		*receive = value;
	}
	return 1U;
}

static void memory_dump_spi_finish(void)
{
	u32 timeout = MEMORY_DUMP_SPI_TIMEOUT;

	while ((SPI2->SR & SPI_SR_TXE) == 0U && timeout != 0U)
	{
		timeout--;
	}
	timeout = MEMORY_DUMP_SPI_TIMEOUT;
	while ((SPI2->SR & SPI_SR_BSY) != 0U && timeout != 0U)
	{
		timeout--;
	}
	if ((SPI2->SR & SPI_SR_RXNE) != 0U)
	{
		(void)*(__IO u8*)&SPI2->DR;
	}
}

static u8 memory_dump_flash_read_status(u8* status)
{
	u8 ok;

	if (status == NULL)
	{
		return 0U;
	}
	MEMORY_DUMP_FLASH_CS_LOW();
	ok = memory_dump_spi_transfer(0x05U, NULL);
	if (ok != 0U)
	{
		ok = memory_dump_spi_transfer(0xFFU, status);
	}
	memory_dump_spi_finish();
	MEMORY_DUMP_FLASH_CS_HIGH();
	return ok;
}

static u8 memory_dump_flash_wait_ready(void)
{
	u32 poll;
	u8 status;

	for (poll = 0U; poll < MEMORY_DUMP_BUSY_POLL_LIMIT; poll++)
	{
		if (memory_dump_flash_read_status(&status) == 0U)
		{
			return 0U;
		}
		if ((status & 0x01U) == 0U)
		{
			return 1U;
		}
	}
	return 0U;
}

static u8 memory_dump_flash_write_enable(void)
{
	u8 ok;

	MEMORY_DUMP_FLASH_CS_LOW();
	ok = memory_dump_spi_transfer(0x06U, NULL);
	memory_dump_spi_finish();
	MEMORY_DUMP_FLASH_CS_HIGH();
	return ok;
}

static u8 memory_dump_flash_init(void)
{
	u8 manufacturer = 0U;
	u8 device_high = 0U;
	u8 device_low = 0U;
	u8 ok;

	memory_dump_spi_init();
	MEMORY_DUMP_FLASH_CS_LOW();
	ok = memory_dump_spi_transfer(0x9FU, NULL);
	if (ok != 0U) ok = memory_dump_spi_transfer(0xFFU, &manufacturer);
	if (ok != 0U) ok = memory_dump_spi_transfer(0xFFU, &device_high);
	if (ok != 0U) ok = memory_dump_spi_transfer(0xFFU, &device_low);
	memory_dump_spi_finish();
	MEMORY_DUMP_FLASH_CS_HIGH();

	return (ok != 0U && manufacturer == W25Q64_JEDEC_MID &&
	        device_high == 0x40U && device_low == 0x17U) ? 1U : 0U;
}

static u8 memory_dump_flash_erase_block(u32 address)
{
	u8 ok;

	if (memory_dump_flash_wait_ready() == 0U ||
	    memory_dump_flash_write_enable() == 0U)
	{
		return 0U;
	}

	MEMORY_DUMP_FLASH_CS_LOW();
	ok = memory_dump_spi_transfer(0xD8U, NULL);
	if (ok != 0U) ok = memory_dump_spi_transfer((u8)(address >> 16U), NULL);
	if (ok != 0U) ok = memory_dump_spi_transfer((u8)(address >> 8U), NULL);
	if (ok != 0U) ok = memory_dump_spi_transfer((u8)address, NULL);
	memory_dump_spi_finish();
	MEMORY_DUMP_FLASH_CS_HIGH();
	return (ok != 0U) ? memory_dump_flash_wait_ready() : 0U;
}

static u8 memory_dump_flash_program(u32 address, const volatile u8* data,
	u16 count, u32* checksum)
{
	u16 index;
	u8 value;
	u8 ok;

	if (data == NULL || count == 0U || count > W25Q64_PAGE_SIZE ||
	    ((address & (W25Q64_PAGE_SIZE - 1U)) + count) > W25Q64_PAGE_SIZE ||
	    memory_dump_flash_wait_ready() == 0U ||
	    memory_dump_flash_write_enable() == 0U)
	{
		return 0U;
	}

	MEMORY_DUMP_FLASH_CS_LOW();
	ok = memory_dump_spi_transfer(0x02U, NULL);
	if (ok != 0U) ok = memory_dump_spi_transfer((u8)(address >> 16U), NULL);
	if (ok != 0U) ok = memory_dump_spi_transfer((u8)(address >> 8U), NULL);
	if (ok != 0U) ok = memory_dump_spi_transfer((u8)address, NULL);
	for (index = 0U; index < count && ok != 0U; index++)
	{
		value = data[index];
		if (checksum != NULL)
		{
			*checksum = memory_dump_checksum_update(*checksum, value);
		}
		ok = memory_dump_spi_transfer(value, NULL);
	}
	memory_dump_spi_finish();
	MEMORY_DUMP_FLASH_CS_HIGH();
	return (ok != 0U) ? memory_dump_flash_wait_ready() : 0U;
}

static u32 memory_dump_checksum_update(u32 checksum, u8 data)
{
	checksum ^= data;
	checksum *= MEMORY_DUMP_FNV_PRIME;
	return checksum;
}

static u32 memory_dump_checksum(const u8* data, u32 length)
{
	u32 index;
	u32 checksum = MEMORY_DUMP_FNV_OFFSET;

	for (index = 0U; index < length; index++)
	{
		checksum = memory_dump_checksum_update(checksum, data[index]);
	}
	return checksum;
}

static void memory_dump_report(Memory_Dump_ProgressCallback_t progress,
	u8 percent, const char* phase, u8* previous)
{
	if (progress != NULL && previous != NULL && percent != *previous)
	{
		*previous = percent;
		progress(percent, phase);
	}
}

Memory_Dump_Result_t Memory_Dump_Capture(
	const Memory_Dump_FaultRecord_t* fault,
	Memory_Dump_ProgressCallback_t progress)
{
	static const u32 bases[3] = {
		MEMORY_DUMP_CCM_BASE, MEMORY_DUMP_MAIN_BASE, MEMORY_DUMP_SRAMEX_BASE
	};
	static const u32 lengths[3] = {
		MEMORY_DUMP_CCM_SIZE, MEMORY_DUMP_MAIN_SIZE, MEMORY_DUMP_SRAMEX_SIZE
	};
	static const u32 flags[3] = {
		MEMORY_DUMP_REGION_FLAG_CCM,
		MEMORY_DUMP_REGION_FLAG_MAIN,
		MEMORY_DUMP_REGION_FLAG_SRAMEX
	};
	u8* header_bytes = (u8*)&s_memory_dump_header;
	u32 flash_offset = MEMORY_DUMP_DATA_OFFSET;
	u32 erase_size;
	u32 erase_blocks;
	u32 block;
	u32 region;
	u32 offset;
	u32 remaining;
	u32 written = 0U;
	u32 checksum = MEMORY_DUMP_FNV_OFFSET;
	u8 previous_percent = 0xFFU;
	u8 selected;
	u16 chunk;

	if (fault == NULL || System_MemoryDumpEnabled() == 0U)
	{
		return MEMORY_DUMP_RESULT_DISABLED;
	}

	memset(&s_memory_dump_header, 0, sizeof(s_memory_dump_header));
	s_memory_dump_header.magic = MEMORY_DUMP_MAGIC;
	s_memory_dump_header.version = MEMORY_DUMP_VERSION;
	s_memory_dump_header.header_size = sizeof(s_memory_dump_header);
	s_memory_dump_header.cpuid = SCB->CPUID;
	memcpy(&s_memory_dump_header.fault, fault, sizeof(*fault));
	s_memory_dump_header.export_state = MEMORY_DUMP_EXPORT_PENDING;

	for (region = 0U; region < 3U; region++)
	{
		selected = System_MemoryDumpRegionEnabled((System_MemoryDumpRegion_t)region);
		if (selected == 0U)
		{
			continue;
		}
		s_memory_dump_header.regions[s_memory_dump_header.region_count].base_address = bases[region];
		s_memory_dump_header.regions[s_memory_dump_header.region_count].length = lengths[region];
		s_memory_dump_header.regions[s_memory_dump_header.region_count].file_offset = flash_offset;
		s_memory_dump_header.region_count++;
		s_memory_dump_header.region_flags |= flags[region];
		s_memory_dump_header.payload_size += lengths[region];
		flash_offset += lengths[region];
	}

	if (s_memory_dump_header.region_count == 0U ||
	    flash_offset > MEMORY_DUMP_FLASH_SIZE)
	{
		return MEMORY_DUMP_RESULT_DISABLED;
	}
	s_memory_dump_header.total_size = flash_offset;
	erase_size = (flash_offset + W25Q64_BLOCK_64K_SIZE - 1U) &
	             ~(W25Q64_BLOCK_64K_SIZE - 1U);
	erase_blocks = erase_size / W25Q64_BLOCK_64K_SIZE;

	memory_dump_report(progress, 0U, "ERASING", &previous_percent);
	if (memory_dump_flash_init() == 0U)
	{
		if (progress != NULL) progress(0U, "FLASH ERROR");
		return MEMORY_DUMP_RESULT_FLASH_ERROR;
	}
	for (block = 0U; block < erase_blocks; block++)
	{
		if (memory_dump_flash_erase_block(MEMORY_DUMP_FLASH_ADDRESS +
		                                  block * W25Q64_BLOCK_64K_SIZE) == 0U)
		{
			if (progress != NULL) progress(0U, "ERASE ERROR");
			return MEMORY_DUMP_RESULT_FLASH_ERROR;
		}
		memory_dump_report(progress,
			(u8)(((block + 1U) * 20U) / erase_blocks),
			"ERASING", &previous_percent);
	}

	for (region = 0U; region < s_memory_dump_header.region_count; region++)
	{
		for (offset = 0U; offset < s_memory_dump_header.regions[region].length;
		     offset += chunk)
		{
			remaining = s_memory_dump_header.regions[region].length - offset;
			chunk = (remaining > W25Q64_PAGE_SIZE) ?
			        (u16)W25Q64_PAGE_SIZE : (u16)remaining;
			if (memory_dump_flash_program(
				MEMORY_DUMP_FLASH_ADDRESS + s_memory_dump_header.regions[region].file_offset + offset,
				(const volatile u8*)(s_memory_dump_header.regions[region].base_address + offset),
				chunk, &checksum) == 0U)
			{
				if (progress != NULL) progress(0U, "WRITE ERROR");
				return MEMORY_DUMP_RESULT_FLASH_ERROR;
			}
			written += chunk;
			memory_dump_report(progress,
				(u8)(20U + ((written * 79U) / s_memory_dump_header.payload_size)),
				"WRITING", &previous_percent);
		}
	}

	s_memory_dump_header.payload_checksum = checksum;
	s_memory_dump_header.header_checksum = memory_dump_checksum(
		header_bytes, (u32)offsetof(Memory_Dump_Header_t, header_checksum));
	if (memory_dump_flash_program(MEMORY_DUMP_FLASH_ADDRESS,
		(const volatile u8*)&s_memory_dump_header,
		(u16)sizeof(s_memory_dump_header), NULL) == 0U)
	{
		if (progress != NULL) progress(0U, "HEADER ERROR");
		return MEMORY_DUMP_RESULT_FLASH_ERROR;
	}

	memory_dump_report(progress, 100U, "SAVED", &previous_percent);
	return MEMORY_DUMP_RESULT_OK;
}

static u8 memory_dump_validate_header(const Memory_Dump_Header_t* header)
{
	u32 index;
	u32 payload_size = 0U;
	u32 expected_offset = MEMORY_DUMP_DATA_OFFSET;

	if (header == NULL || header->magic != MEMORY_DUMP_MAGIC ||
	    header->version != MEMORY_DUMP_VERSION ||
	    header->header_size != sizeof(Memory_Dump_Header_t) ||
	    header->region_count == 0U || header->region_count > 3U ||
	    header->total_size < MEMORY_DUMP_DATA_OFFSET ||
	    header->total_size > MEMORY_DUMP_FLASH_SIZE ||
	    header->header_checksum != memory_dump_checksum(
		(const u8*)header, (u32)offsetof(Memory_Dump_Header_t, header_checksum)))
	{
		return 0U;
	}

	for (index = 0U; index < header->region_count; index++)
	{
		if (header->regions[index].file_offset != expected_offset ||
		    header->regions[index].length == 0U ||
		    header->regions[index].length >
		        (header->total_size - header->regions[index].file_offset))
		{
			return 0U;
		}
		payload_size += header->regions[index].length;
		expected_offset += header->regions[index].length;
	}

	return (payload_size == header->payload_size &&
	        expected_offset == header->total_size) ? 1U : 0U;
}

Memory_Dump_Result_t Memory_Dump_ExportPending(void)
{
	Memory_Dump_Header_t header;
	FIL* file;
	u8* buffer;
	u32 offset = 0U;
	u32 checksum = MEMORY_DUMP_FNV_OFFSET;
	u32 exported = MEMORY_DUMP_EXPORT_COMPLETE;
	u32 index;
	u32 chunk;
	UINT written;
	FRESULT fr;
	u8 file_opened = 0U;
	u8 file_created = 0U;
	u8 file_complete = 0U;
	Memory_Dump_Result_t result = MEMORY_DUMP_RESULT_OK;

	if (W25Q64_ReadData(MEMORY_DUMP_FLASH_ADDRESS, (u8*)&header,
	                    sizeof(header)) != W25Q64_OK)
	{
		return MEMORY_DUMP_RESULT_FLASH_ERROR;
	}
	if (header.magic == 0xFFFFFFFFUL)
	{
		return MEMORY_DUMP_RESULT_NONE;
	}
	if (memory_dump_validate_header(&header) == 0U)
	{
		return MEMORY_DUMP_RESULT_INVALID;
	}
	if (header.export_state != MEMORY_DUMP_EXPORT_PENDING)
	{
		return MEMORY_DUMP_RESULT_ALREADY_EXPORTED;
	}

	file = (FIL*)sys_malloc(SRAMEX, sizeof(FIL));
	buffer = (u8*)sys_malloc(SRAMEX, MEMORY_DUMP_IO_BUFFER_SIZE);
	if (file == NULL || buffer == NULL)
	{
		if (buffer != NULL)
		{
			sys_free(SRAMEX, buffer);
		}
		if (file != NULL)
		{
			sys_free(SRAMEX, file);
		}
		return MEMORY_DUMP_RESULT_NO_MEMORY;
	}
	memset(file, 0, sizeof(*file));
	fr = f_open(file, MEMORY_DUMP_EXPORT_PATH, FA_CREATE_ALWAYS | FA_WRITE);
	if (fr != FR_OK)
	{
		result = MEMORY_DUMP_RESULT_FATFS_UNAVAILABLE;
		goto cleanup;
	}
	file_opened = 1U;
	file_created = 1U;

	while (offset < header.total_size)
	{
		chunk = header.total_size - offset;
		if (chunk > MEMORY_DUMP_IO_BUFFER_SIZE)
		{
			chunk = MEMORY_DUMP_IO_BUFFER_SIZE;
		}
		if (W25Q64_ReadData(MEMORY_DUMP_FLASH_ADDRESS + offset,
		                    buffer, chunk) != W25Q64_OK)
		{
			result = MEMORY_DUMP_RESULT_FLASH_ERROR;
			goto cleanup;
		}
		if (offset + chunk > MEMORY_DUMP_DATA_OFFSET)
		{
			u32 start = (offset < MEMORY_DUMP_DATA_OFFSET) ?
			            (MEMORY_DUMP_DATA_OFFSET - offset) : 0U;
			for (index = start; index < chunk; index++)
			{
				checksum = memory_dump_checksum_update(checksum, buffer[index]);
			}
		}
		written = 0U;
		fr = f_write(file, buffer, (UINT)chunk, &written);
		if (fr != FR_OK || written != (UINT)chunk)
		{
			result = MEMORY_DUMP_RESULT_FILE_ERROR;
			goto cleanup;
		}
		offset += chunk;
	}

	if (checksum != header.payload_checksum)
	{
		result = MEMORY_DUMP_RESULT_CHECKSUM_ERROR;
		goto cleanup;
	}
	if (f_sync(file) != FR_OK)
	{
		result = MEMORY_DUMP_RESULT_FILE_ERROR;
		goto cleanup;
	}
	if (f_close(file) != FR_OK)
	{
		file_opened = 0U;
		result = MEMORY_DUMP_RESULT_FILE_ERROR;
		goto cleanup;
	}
	file_opened = 0U;
	file_complete = 1U;

	if (W25Q64_PageProgram(
		MEMORY_DUMP_FLASH_ADDRESS + (u32)offsetof(Memory_Dump_Header_t, export_state),
		(const u8*)&exported, sizeof(exported)) != W25Q64_OK)
	{
		result = MEMORY_DUMP_RESULT_FLASH_ERROR;
	}

cleanup:
	if (file_opened != 0U)
	{
		(void)f_close(file);
	}
	if (result != MEMORY_DUMP_RESULT_OK && file_created != 0U &&
	    file_complete == 0U)
	{
		(void)f_unlink(MEMORY_DUMP_EXPORT_PATH);
	}
	sys_free(SRAMEX, buffer);
	sys_free(SRAMEX, file);
	return result;
}

const char* Memory_Dump_ResultText(Memory_Dump_Result_t result)
{
	switch (result)
	{
		case MEMORY_DUMP_RESULT_OK:
			return "memory dump exported";
		case MEMORY_DUMP_RESULT_DISABLED:
			return "memory dump disabled";
		case MEMORY_DUMP_RESULT_NONE:
			return "no memory dump";
		case MEMORY_DUMP_RESULT_ALREADY_EXPORTED:
			return "memory dump already exported";
		case MEMORY_DUMP_RESULT_FLASH_ERROR:
			return "memory dump flash error";
		case MEMORY_DUMP_RESULT_INVALID:
			return "memory dump header invalid";
		case MEMORY_DUMP_RESULT_NO_MEMORY:
			return "memory dump buffer unavailable";
		case MEMORY_DUMP_RESULT_FATFS_UNAVAILABLE:
			return "memory dump FatFs unavailable";
		case MEMORY_DUMP_RESULT_FILE_ERROR:
			return "memory dump file error";
		case MEMORY_DUMP_RESULT_CHECKSUM_ERROR:
			return "memory dump checksum error";
		default:
			return "memory dump error";
	}
}
