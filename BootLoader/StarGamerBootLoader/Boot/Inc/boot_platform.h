#ifndef BOOT_PLATFORM_H
#define BOOT_PLATFORM_H

#include "stm32f4xx_hal.h"

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;

extern SPI_HandleTypeDef hspi1;
extern SPI_HandleTypeDef hspi2;
extern TIM_HandleTypeDef htim13;

#endif
