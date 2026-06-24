#ifndef AT24C02_H
#define AT24C02_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sys.h"

#define AT24C02_SIZE             256U
#define AT24C02_PAGE_SIZE        8U

typedef enum
{
    AT24C02_OK = 0,
    AT24C02_ERROR,
    AT24C02_ACK_ERROR,
    AT24C02_TIMEOUT,
    AT24C02_PARAM_ERROR
} AT24C02_StatusTypeDef;

AT24C02_StatusTypeDef AT24C02_Init(void);
AT24C02_StatusTypeDef AT24C02_IsReady(void);
AT24C02_StatusTypeDef AT24C02_Write(u8 address, const u8 *data, u16 count);
AT24C02_StatusTypeDef AT24C02_Read(u8 address, u8 *data, u16 count);
AT24C02_StatusTypeDef AT24C02_WriteByte(u8 address, u8 data);
AT24C02_StatusTypeDef AT24C02_ReadByte(u8 address, u8 *data);

#ifdef __cplusplus
}
#endif

#endif
