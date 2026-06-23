#include "w25q64.h"
#include "main.h"
#include <string.h>

extern SPI_HandleTypeDef hspi2;

#ifndef W25Q64_CS_GPIO_Port
#define W25Q64_CS_GPIO_Port    GPIOE
#endif

#ifndef W25Q64_CS_Pin
#define W25Q64_CS_Pin          GPIO_PIN_6
#endif

#define W25Q64_WRITE_ENABLE                0x06U
#define W25Q64_WRITE_DISABLE               0x04U
#define W25Q64_READ_STATUS_REGISTER_1      0x05U
#define W25Q64_PAGE_PROGRAM                0x02U
#define W25Q64_SECTOR_ERASE_4KB            0x20U
#define W25Q64_BLOCK_ERASE_32KB            0x52U
#define W25Q64_BLOCK_ERASE_64KB            0xD8U
#define W25Q64_CHIP_ERASE                  0xC7U
#define W25Q64_JEDEC_ID                    0x9FU
#define W25Q64_READ_DATA                   0x03U
#define W25Q64_DUMMY_BYTE                  0xFFU

#define W25Q64_SPI_TIMEOUT_MS              100U
#define W25Q64_PROGRAM_TIMEOUT_MS          1000U
#define W25Q64_ERASE_TIMEOUT_MS            5000U
#define W25Q64_CHIP_ERASE_TIMEOUT_MS       100000U
#define W25Q64_RX_CHUNK_SIZE               64U

static void W25Q64_Select(void)
{
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_RESET);
}

static void W25Q64_Unselect(void)
{
    HAL_GPIO_WritePin(W25Q64_CS_GPIO_Port, W25Q64_CS_Pin, GPIO_PIN_SET);
}

static W25Q64_StatusTypeDef W25Q64_FromHAL(HAL_StatusTypeDef status)
{
    if (status == HAL_OK)
    {
        return W25Q64_OK;
    }

    if (status == HAL_TIMEOUT)
    {
        return W25Q64_TIMEOUT;
    }

    return W25Q64_ERROR;
}

static W25Q64_StatusTypeDef W25Q64_CheckRange(uint32_t address, uint32_t count)
{
    if (count == 0U)
    {
        return W25Q64_OK;
    }

    if ((address >= W25Q64_FLASH_SIZE) || (count > (W25Q64_FLASH_SIZE - address)))
    {
        return W25Q64_PARAM_ERROR;
    }

    return W25Q64_OK;
}

static W25Q64_StatusTypeDef W25Q64_SendCommand(uint8_t command)
{
    W25Q64_StatusTypeDef status;

    W25Q64_Select();
    status = W25Q64_FromHAL(HAL_SPI_Transmit(&hspi2, &command, 1U, W25Q64_SPI_TIMEOUT_MS));
    W25Q64_Unselect();

    return status;
}

static W25Q64_StatusTypeDef W25Q64_SendCommandAddress(uint8_t command, uint32_t address)
{
    uint8_t buffer[4];

    buffer[0] = command;
    buffer[1] = (uint8_t)(address >> 16);
    buffer[2] = (uint8_t)(address >> 8);
    buffer[3] = (uint8_t)address;

    return W25Q64_FromHAL(HAL_SPI_Transmit(&hspi2, buffer, sizeof(buffer), W25Q64_SPI_TIMEOUT_MS));
}

static W25Q64_StatusTypeDef W25Q64_ReadBytes(uint8_t *data, uint32_t count)
{
    uint8_t dummy[W25Q64_RX_CHUNK_SIZE];
    W25Q64_StatusTypeDef status;

    memset(dummy, W25Q64_DUMMY_BYTE, sizeof(dummy));

    while (count > 0U)
    {
        uint16_t chunk = (count > W25Q64_RX_CHUNK_SIZE) ? W25Q64_RX_CHUNK_SIZE : (uint16_t)count;
        status = W25Q64_FromHAL(HAL_SPI_TransmitReceive(&hspi2, dummy, data, chunk, W25Q64_SPI_TIMEOUT_MS));
        if (status != W25Q64_OK)
        {
            return status;
        }

        data += chunk;
        count -= chunk;
    }

    return W25Q64_OK;
}

W25Q64_StatusTypeDef W25Q64_Init(void)
{
//		uint8_t mid;
//		uint16_t did;
    W25Q64_Unselect();
//		W25Q64_ReadID(&mid,&did);
//		if(mid == 0x00 || mid == 0xff || did == 0x0000 || did == 0xffff)
//		{
//			return W25Q64_ERROR;
//		}
    return W25Q64_OK;
}

W25Q64_StatusTypeDef W25Q64_ReadID(uint8_t *mid, uint16_t *did)
{
    uint8_t tx[4] = {W25Q64_JEDEC_ID, W25Q64_DUMMY_BYTE, W25Q64_DUMMY_BYTE, W25Q64_DUMMY_BYTE};
    uint8_t rx[4] = {0};
    W25Q64_StatusTypeDef status;

    if ((mid == NULL) || (did == NULL))
    {
        return W25Q64_PARAM_ERROR;
    }

    W25Q64_Select();
    status = W25Q64_FromHAL(HAL_SPI_TransmitReceive(&hspi2, tx, rx, sizeof(tx), W25Q64_SPI_TIMEOUT_MS));
    W25Q64_Unselect();

    if (status == W25Q64_OK)
    {
        *mid = rx[1];
        *did = ((uint16_t)rx[2] << 8) | rx[3];
    }

    return status;
}

W25Q64_StatusTypeDef W25Q64_ReadJEDECID(uint32_t *jedec_id)
{
    uint8_t mid;
    uint16_t did;
    W25Q64_StatusTypeDef status;

    if (jedec_id == NULL)
    {
        return W25Q64_PARAM_ERROR;
    }

    status = W25Q64_ReadID(&mid, &did);
    if (status == W25Q64_OK)
    {
        *jedec_id = ((uint32_t)mid << 16) | did;
    }

    return status;
}

W25Q64_StatusTypeDef W25Q64_ReadStatusRegister1(uint8_t *status_reg)
{
    uint8_t tx[2] = {W25Q64_READ_STATUS_REGISTER_1, W25Q64_DUMMY_BYTE};
    uint8_t rx[2] = {0};
    W25Q64_StatusTypeDef status;

    if (status_reg == NULL)
    {
        return W25Q64_PARAM_ERROR;
    }

    W25Q64_Select();
    status = W25Q64_FromHAL(HAL_SPI_TransmitReceive(&hspi2, tx, rx, sizeof(tx), W25Q64_SPI_TIMEOUT_MS));
    W25Q64_Unselect();

    if (status == W25Q64_OK)
    {
        *status_reg = rx[1];
    }

    return status;
}

W25Q64_StatusTypeDef W25Q64_WriteEnable(void)
{
    return W25Q64_SendCommand(W25Q64_WRITE_ENABLE);
}

W25Q64_StatusTypeDef W25Q64_WriteDisable(void)
{
    return W25Q64_SendCommand(W25Q64_WRITE_DISABLE);
}

W25Q64_StatusTypeDef W25Q64_WaitBusy(uint32_t timeout_ms)
{
    uint8_t status_reg;
    uint32_t start_tick = HAL_GetTick();
    W25Q64_StatusTypeDef status;

    do
    {
        status = W25Q64_ReadStatusRegister1(&status_reg);
        if (status != W25Q64_OK)
        {
            return status;
        }

        if ((status_reg & 0x01U) == 0U)
        {
            return W25Q64_OK;
        }
    } while ((HAL_GetTick() - start_tick) <= timeout_ms);

    return W25Q64_TIMEOUT;
}

W25Q64_StatusTypeDef W25Q64_ReadData(uint32_t address, uint8_t *data, uint32_t count)
{
    W25Q64_StatusTypeDef status;

    if ((data == NULL) && (count > 0U))
    {
        return W25Q64_PARAM_ERROR;
    }

    status = W25Q64_CheckRange(address, count);
    if ((status != W25Q64_OK) || (count == 0U))
    {
        return status;
    }

    W25Q64_Select();
    status = W25Q64_SendCommandAddress(W25Q64_READ_DATA, address);
    if (status == W25Q64_OK)
    {
        status = W25Q64_ReadBytes(data, count);
    }
    W25Q64_Unselect();

    return status;
}

W25Q64_StatusTypeDef W25Q64_PageProgram(uint32_t address, const uint8_t *data, uint16_t count)
{
    W25Q64_StatusTypeDef status;

    if ((data == NULL) && (count > 0U))
    {
        return W25Q64_PARAM_ERROR;
    }

    if ((count > W25Q64_PAGE_SIZE) || (((address & (W25Q64_PAGE_SIZE - 1U)) + count) > W25Q64_PAGE_SIZE))
    {
        return W25Q64_PARAM_ERROR;
    }

    status = W25Q64_CheckRange(address, count);
    if ((status != W25Q64_OK) || (count == 0U))
    {
        return status;
    }

    status = W25Q64_WaitBusy(W25Q64_PROGRAM_TIMEOUT_MS);
    if (status != W25Q64_OK)
    {
        return status;
    }

    status = W25Q64_WriteEnable();
    if (status != W25Q64_OK)
    {
        return status;
    }

    W25Q64_Select();
    status = W25Q64_SendCommandAddress(W25Q64_PAGE_PROGRAM, address);
    if (status == W25Q64_OK)
    {
        status = W25Q64_FromHAL(HAL_SPI_Transmit(&hspi2, (uint8_t *)data, count, W25Q64_SPI_TIMEOUT_MS));
    }
    W25Q64_Unselect();

    if (status != W25Q64_OK)
    {
        return status;
    }

    return W25Q64_WaitBusy(W25Q64_PROGRAM_TIMEOUT_MS);
}

W25Q64_StatusTypeDef W25Q64_WriteData(uint32_t address, const uint8_t *data, uint32_t count)
{
    W25Q64_StatusTypeDef status;

    if ((data == NULL) && (count > 0U))
    {
        return W25Q64_PARAM_ERROR;
    }

    status = W25Q64_CheckRange(address, count);
    if ((status != W25Q64_OK) || (count == 0U))
    {
        return status;
    }

    while (count > 0U)
    {
        uint32_t page_left = W25Q64_PAGE_SIZE - (address & (W25Q64_PAGE_SIZE - 1U));
        uint16_t chunk = (count > page_left) ? (uint16_t)page_left : (uint16_t)count;

        status = W25Q64_PageProgram(address, data, chunk);
        if (status != W25Q64_OK)
        {
            return status;
        }

        address += chunk;
        data += chunk;
        count -= chunk;
    }

    return W25Q64_OK;
}

static W25Q64_StatusTypeDef W25Q64_Erase(uint8_t command, uint32_t address, uint32_t timeout_ms)
{
    W25Q64_StatusTypeDef status;

    status = W25Q64_CheckRange(address, 1U);
    if (status != W25Q64_OK)
    {
        return status;
    }

    status = W25Q64_WaitBusy(timeout_ms);
    if (status != W25Q64_OK)
    {
        return status;
    }

    status = W25Q64_WriteEnable();
    if (status != W25Q64_OK)
    {
        return status;
    }

    W25Q64_Select();
    status = W25Q64_SendCommandAddress(command, address);
    W25Q64_Unselect();

    if (status != W25Q64_OK)
    {
        return status;
    }

    return W25Q64_WaitBusy(timeout_ms);
}

W25Q64_StatusTypeDef W25Q64_SectorErase(uint32_t address)
{
    return W25Q64_Erase(W25Q64_SECTOR_ERASE_4KB, address, W25Q64_ERASE_TIMEOUT_MS);
}

W25Q64_StatusTypeDef W25Q64_BlockErase32K(uint32_t address)
{
    return W25Q64_Erase(W25Q64_BLOCK_ERASE_32KB, address, W25Q64_ERASE_TIMEOUT_MS);
}

W25Q64_StatusTypeDef W25Q64_BlockErase64K(uint32_t address)
{
    return W25Q64_Erase(W25Q64_BLOCK_ERASE_64KB, address, W25Q64_ERASE_TIMEOUT_MS);
}

W25Q64_StatusTypeDef W25Q64_ChipErase(void)
{
    W25Q64_StatusTypeDef status;

    status = W25Q64_WaitBusy(W25Q64_ERASE_TIMEOUT_MS);
    if (status != W25Q64_OK)
    {
        return status;
    }

    status = W25Q64_WriteEnable();
    if (status != W25Q64_OK)
    {
        return status;
    }

    status = W25Q64_SendCommand(W25Q64_CHIP_ERASE);
    if (status != W25Q64_OK)
    {
        return status;
    }

    return W25Q64_WaitBusy(W25Q64_CHIP_ERASE_TIMEOUT_MS);
}
