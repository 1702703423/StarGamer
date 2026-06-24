#include "at24c02.h"
#include "main.h"

#define AT24C02_SCL_PIN              GPIO_PIN_8
#define AT24C02_SDA_PIN              GPIO_PIN_9
#define AT24C02_GPIO_PORT            GPIOB

#define AT24C02_DEVICE_ADDR_WRITE    0xA0U
#define AT24C02_DEVICE_ADDR_READ     0xA1U
#define AT24C02_ACK_TIMEOUT          250U
#define AT24C02_READY_TIMEOUT_MS     20U
#define AT24C02_I2C_DELAY_LOOP       400U

#define AT24C02_SCL_HIGH()           do { PBout(8) = 1U; } while (0)
#define AT24C02_SCL_LOW()            do { PBout(8) = 0U; } while (0)
#define AT24C02_SDA_HIGH()           do { PBout(9) = 1U; } while (0)
#define AT24C02_SDA_LOW()            do { PBout(9) = 0U; } while (0)
#define AT24C02_SDA_READ()           ((u8)PBin(9))

static void AT24C02_Delay(void)
{
    volatile u32 i;

    for (i = 0U; i < AT24C02_I2C_DELAY_LOOP; i++)
    {
        __NOP();
    }
}

static void AT24C02_Start(void)
{
    AT24C02_SDA_HIGH();
    AT24C02_SCL_HIGH();
    AT24C02_Delay();
    AT24C02_SDA_LOW();
    AT24C02_Delay();
    AT24C02_SCL_LOW();
    AT24C02_Delay();
}

static void AT24C02_Stop(void)
{
    AT24C02_SCL_LOW();
    AT24C02_SDA_LOW();
    AT24C02_Delay();
    AT24C02_SCL_HIGH();
    AT24C02_Delay();
    AT24C02_SDA_HIGH();
    AT24C02_Delay();
}

static void AT24C02_SendAck(u8 ack)
{
    AT24C02_SCL_LOW();
    if (ack != 0U)
    {
        AT24C02_SDA_LOW();
    }
    else
    {
        AT24C02_SDA_HIGH();
    }

    AT24C02_Delay();
    AT24C02_SCL_HIGH();
    AT24C02_Delay();
    AT24C02_SCL_LOW();
    AT24C02_Delay();
    AT24C02_SDA_HIGH();
}

static u8 AT24C02_WaitAck(void)
{
    u16 timeout = AT24C02_ACK_TIMEOUT;

    AT24C02_SCL_LOW();
    AT24C02_SDA_HIGH();
    AT24C02_Delay();
    AT24C02_SCL_HIGH();
    AT24C02_Delay();

    while (AT24C02_SDA_READ() != 0U)
    {
        if (timeout == 0U)
        {
            AT24C02_SCL_LOW();
            AT24C02_Delay();
            return 0U;
        }

        timeout--;
    }

    AT24C02_SCL_LOW();
    AT24C02_Delay();
    return 1U;
}

static void AT24C02_SendByte(u8 data)
{
    u8 mask;

    for (mask = 0x80U; mask != 0U; mask >>= 1)
    {
        AT24C02_SCL_LOW();
        if ((data & mask) != 0U)
        {
            AT24C02_SDA_HIGH();
        }
        else
        {
            AT24C02_SDA_LOW();
        }

        AT24C02_Delay();
        AT24C02_SCL_HIGH();
        AT24C02_Delay();
    }

    AT24C02_SCL_LOW();
    AT24C02_Delay();
    AT24C02_SDA_HIGH();
}

static u8 AT24C02_ReadDataByte(u8 ack)
{
    u8 i;
    u8 data = 0U;

    AT24C02_SDA_HIGH();
    for (i = 0U; i < 8U; i++)
    {
        AT24C02_SCL_LOW();
        AT24C02_Delay();
        AT24C02_SCL_HIGH();
        AT24C02_Delay();
        data <<= 1;
        if (AT24C02_SDA_READ() != 0U)
        {
            data |= 0x01U;
        }
    }

    AT24C02_SendAck(ack);
    return data;
}

static AT24C02_StatusTypeDef AT24C02_SendByteWithAck(u8 data)
{
    AT24C02_SendByte(data);
    if (AT24C02_WaitAck() == 0U)
    {
        return AT24C02_ACK_ERROR;
    }

    return AT24C02_OK;
}

static AT24C02_StatusTypeDef AT24C02_CheckRange(u8 address, u16 count)
{
    if (count == 0U)
    {
        return AT24C02_OK;
    }

    if (((u16)address >= AT24C02_SIZE) || (count > (AT24C02_SIZE - (u16)address)))
    {
        return AT24C02_PARAM_ERROR;
    }

    return AT24C02_OK;
}

static void AT24C02_BusRecover(void)
{
    u8 i;

    AT24C02_SDA_HIGH();
    AT24C02_SCL_HIGH();
    AT24C02_Delay();

    if (AT24C02_SDA_READ() == 0U)
    {
        for (i = 0U; i < 9U; i++)
        {
            AT24C02_SCL_LOW();
            AT24C02_Delay();
            AT24C02_SCL_HIGH();
            AT24C02_Delay();
        }
    }

    AT24C02_Stop();
}

AT24C02_StatusTypeDef AT24C02_Init(void)
{
    GPIO_InitTypeDef GPIO_InitStruct = {0};

    __HAL_RCC_GPIOB_CLK_ENABLE();

    HAL_GPIO_WritePin(AT24C02_GPIO_PORT, AT24C02_SCL_PIN | AT24C02_SDA_PIN, GPIO_PIN_SET);

    GPIO_InitStruct.Pin = AT24C02_SCL_PIN | AT24C02_SDA_PIN;
    GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_OD;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(AT24C02_GPIO_PORT, &GPIO_InitStruct);

    AT24C02_BusRecover();

    return AT24C02_IsReady();
}

AT24C02_StatusTypeDef AT24C02_IsReady(void)
{
    u32 start_tick = HAL_GetTick();

    do
    {
        AT24C02_Start();
        AT24C02_SendByte(AT24C02_DEVICE_ADDR_WRITE);
        if (AT24C02_WaitAck() != 0U)
        {
            AT24C02_Stop();
            return AT24C02_OK;
        }
        AT24C02_Stop();
        HAL_Delay(1U);
    } while ((HAL_GetTick() - start_tick) <= AT24C02_READY_TIMEOUT_MS);

    return AT24C02_TIMEOUT;
}

static AT24C02_StatusTypeDef AT24C02_WritePage(u8 address, const u8 *data, u8 count)
{
    AT24C02_StatusTypeDef status;
    u8 i;

    status = AT24C02_IsReady();
    if (status != AT24C02_OK)
    {
        return status;
    }

    AT24C02_Start();

    status = AT24C02_SendByteWithAck(AT24C02_DEVICE_ADDR_WRITE);
    if (status == AT24C02_OK)
    {
        status = AT24C02_SendByteWithAck(address);
    }

    for (i = 0U; (i < count) && (status == AT24C02_OK); i++)
    {
        status = AT24C02_SendByteWithAck(data[i]);
    }

    AT24C02_Stop();
    if (status != AT24C02_OK)
    {
        return status;
    }

    return AT24C02_IsReady();
}

AT24C02_StatusTypeDef AT24C02_Write(u8 address, const u8 *data, u16 count)
{
    AT24C02_StatusTypeDef status;
    u16 offset = address;

    if ((data == NULL) && (count > 0U))
    {
        return AT24C02_PARAM_ERROR;
    }

    status = AT24C02_CheckRange(address, count);
    if ((status != AT24C02_OK) || (count == 0U))
    {
        return status;
    }

    while (count > 0U)
    {
        u8 page_left = (u8)(AT24C02_PAGE_SIZE - (offset % AT24C02_PAGE_SIZE));
        u8 chunk = (count > page_left) ? page_left : (u8)count;

        status = AT24C02_WritePage((u8)offset, data, chunk);
        if (status != AT24C02_OK)
        {
            return status;
        }

        offset += chunk;
        data += chunk;
        count -= chunk;
    }

    return AT24C02_OK;
}

AT24C02_StatusTypeDef AT24C02_Read(u8 address, u8 *data, u16 count)
{
    AT24C02_StatusTypeDef status;
    u16 i;

    if ((data == NULL) && (count > 0U))
    {
        return AT24C02_PARAM_ERROR;
    }

    status = AT24C02_CheckRange(address, count);
    if ((status != AT24C02_OK) || (count == 0U))
    {
        return status;
    }

    status = AT24C02_IsReady();
    if (status != AT24C02_OK)
    {
        return status;
    }

    AT24C02_Start();

    status = AT24C02_SendByteWithAck(AT24C02_DEVICE_ADDR_WRITE);
    if (status == AT24C02_OK)
    {
        status = AT24C02_SendByteWithAck(address);
    }
    if (status == AT24C02_OK)
    {
        AT24C02_Start();
        status = AT24C02_SendByteWithAck(AT24C02_DEVICE_ADDR_READ);
    }

    if (status != AT24C02_OK)
    {
        AT24C02_Stop();
        return status;
    }

    for (i = 0U; i < count; i++)
    {
        data[i] = AT24C02_ReadDataByte((i + 1U) < count);
    }

    AT24C02_Stop();
    return AT24C02_OK;
}

AT24C02_StatusTypeDef AT24C02_WriteByte(u8 address, u8 data)
{
    return AT24C02_Write(address, &data, 1U);
}

AT24C02_StatusTypeDef AT24C02_ReadByte(u8 address, u8 *data)
{
    return AT24C02_Read(address, data, 1U);
}
