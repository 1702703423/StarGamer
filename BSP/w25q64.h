#ifndef W25Q64_H
#define W25Q64_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>

#define W25Q64_FLASH_SIZE          (8UL * 1024UL * 1024UL)
#define W25Q64_PAGE_SIZE           256U
#define W25Q64_SECTOR_SIZE         4096U
#define W25Q64_BLOCK_32K_SIZE      (32UL * 1024UL)
#define W25Q64_BLOCK_64K_SIZE      (64UL * 1024UL)

#define W25Q64_JEDEC_MID           0xEFU
#define W25Q64_JEDEC_DID           0x4017U

typedef enum
{
    W25Q64_OK = 0,
    W25Q64_ERROR,
    W25Q64_TIMEOUT,
    W25Q64_PARAM_ERROR
} W25Q64_StatusTypeDef;

W25Q64_StatusTypeDef W25Q64_Init(void);
W25Q64_StatusTypeDef W25Q64_ReadID(uint8_t *mid, uint16_t *did);
W25Q64_StatusTypeDef W25Q64_ReadJEDECID(uint32_t *jedec_id);
W25Q64_StatusTypeDef W25Q64_ReadStatusRegister1(uint8_t *status);
W25Q64_StatusTypeDef W25Q64_WriteEnable(void);
W25Q64_StatusTypeDef W25Q64_WriteDisable(void);
W25Q64_StatusTypeDef W25Q64_WaitBusy(uint32_t timeout_ms);

W25Q64_StatusTypeDef W25Q64_ReadData(uint32_t address, uint8_t *data, uint32_t count);
W25Q64_StatusTypeDef W25Q64_PageProgram(uint32_t address, const uint8_t *data, uint16_t count);
W25Q64_StatusTypeDef W25Q64_WriteData(uint32_t address, const uint8_t *data, uint32_t count);
W25Q64_StatusTypeDef W25Q64_SectorErase(uint32_t address);
W25Q64_StatusTypeDef W25Q64_BlockErase32K(uint32_t address);
W25Q64_StatusTypeDef W25Q64_BlockErase64K(uint32_t address);
W25Q64_StatusTypeDef W25Q64_ChipErase(void);

#ifdef __cplusplus
}
#endif

#endif
