/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : usbd_storage_if.c
  * @version        : v1.0_Cube
  * @brief          : Memory management layer.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "usbd_storage_if.h"

/* USER CODE BEGIN INCLUDE */
#include "bsp_driver_sd.h"
#include "sys.h"
#include <string.h>

/* USER CODE END INCLUDE */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/

/* USER CODE BEGIN PV */
/* Private variables ---------------------------------------------------------*/

/* USER CODE END PV */

/** @addtogroup STM32_USB_OTG_DEVICE_LIBRARY
  * @brief Usb device.
  * @{
  */

/** @defgroup USBD_STORAGE
  * @brief Usb mass storage device module
  * @{
  */

/** @defgroup USBD_STORAGE_Private_TypesDefinitions
  * @brief Private types.
  * @{
  */

/* USER CODE BEGIN PRIVATE_TYPES */

/* USER CODE END PRIVATE_TYPES */

/**
  * @}
  */

/** @defgroup USBD_STORAGE_Private_Defines
  * @brief Private defines.
  * @{
  */

#define STORAGE_LUN_NBR                  1
#define STORAGE_BLK_NBR                  0x10000
#define STORAGE_BLK_SIZ                  0x200

/* USER CODE BEGIN PRIVATE_DEFINES */
#define STORAGE_SD_BLOCK_SIZE             512U
#define STORAGE_SD_TIMEOUT_MS             30000U
#define STORAGE_SD_WRITE_PROTECTED        0U

/* USER CODE END PRIVATE_DEFINES */

/**
  * @}
  */

/** @defgroup USBD_STORAGE_Private_Macros
  * @brief Private macros.
  * @{
  */

/* USER CODE BEGIN PRIVATE_MACRO */

/* USER CODE END PRIVATE_MACRO */

/**
  * @}
  */

/** @defgroup USBD_STORAGE_Private_Variables
  * @brief Private variables.
  * @{
  */

/* USER CODE BEGIN INQUIRY_DATA_FS */
/** USB Mass storage Standard Inquiry Data. */
const int8_t STORAGE_Inquirydata_FS[] = {/* 36 */

  /* LUN 0 */
  0x00,
  0x80,
  0x02,
  0x02,
  (STANDARD_INQUIRY_DATA_LEN - 5),
  0x00,
  0x00,
  0x00,
  'S', 't', 'a', 'r', 'G', 'a', 'm', 'e', /* Manufacturer : 8 bytes */
  'S', 'D', ' ', 'C', 'a', 'r', 'd', ' ', /* Product      : 16 Bytes */
  'R', 'e', 'a', 'd', 'e', 'r', ' ', ' ',
  '1', '.', '0' ,'0'                      /* Version      : 4 Bytes */
};
/* USER CODE END INQUIRY_DATA_FS */

/* USER CODE BEGIN PRIVATE_VARIABLES */
static u8 s_sd_initialized;
static HAL_SD_CardInfoTypeDef s_sd_card_info;
static u32 s_sd_scratch_word[STORAGE_SD_BLOCK_SIZE / 4U];

/* USER CODE END PRIVATE_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_STORAGE_Exported_Variables
  * @brief Public variables.
  * @{
  */

extern USBD_HandleTypeDef hUsbDeviceFS;

/* USER CODE BEGIN EXPORTED_VARIABLES */

/* USER CODE END EXPORTED_VARIABLES */

/**
  * @}
  */

/** @defgroup USBD_STORAGE_Private_FunctionPrototypes
  * @brief Private functions declaration.
  * @{
  */

static int8_t STORAGE_Init_FS(uint8_t lun);
static int8_t STORAGE_GetCapacity_FS(uint8_t lun, uint32_t *block_num, uint16_t *block_size);
static int8_t STORAGE_IsReady_FS(uint8_t lun);
static int8_t STORAGE_IsWriteProtected_FS(uint8_t lun);
static int8_t STORAGE_Read_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_Write_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len);
static int8_t STORAGE_GetMaxLun_FS(void);

/* USER CODE BEGIN PRIVATE_FUNCTIONS_DECLARATION */
static int8_t STORAGE_SD_CheckLun(u8 lun);
static u8 STORAGE_SD_EnsureInit(void);
static u8 STORAGE_SD_RefreshInfo(void);
static u8 STORAGE_SD_WaitReady(u32 timeout_ms);
static u8 STORAGE_SD_IsAligned(const void *ptr);
static int8_t STORAGE_SD_ReadBlocks(u8 *buf, u32 blk_addr, u16 blk_len);
static int8_t STORAGE_SD_WriteBlocks(u8 *buf, u32 blk_addr, u16 blk_len);

/* USER CODE END PRIVATE_FUNCTIONS_DECLARATION */

/**
  * @}
  */

USBD_StorageTypeDef USBD_Storage_Interface_fops_FS =
{
  STORAGE_Init_FS,
  STORAGE_GetCapacity_FS,
  STORAGE_IsReady_FS,
  STORAGE_IsWriteProtected_FS,
  STORAGE_Read_FS,
  STORAGE_Write_FS,
  STORAGE_GetMaxLun_FS,
  (int8_t *)STORAGE_Inquirydata_FS
};

/* Private functions ---------------------------------------------------------*/
/**
  * @brief  Initializes the storage unit (medium) over USB FS IP
  * @param  lun: Logical unit number.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_Init_FS(uint8_t lun)
{
  /* USER CODE BEGIN 2 */
  if (STORAGE_SD_CheckLun((u8)lun) != USBD_OK)
  {
    return (USBD_FAIL);
  }

  return (STORAGE_SD_EnsureInit() != 0U) ? (USBD_OK) : (USBD_FAIL);
  /* USER CODE END 2 */
}

/**
  * @brief  Returns the medium capacity.
  * @param  lun: Logical unit number.
  * @param  block_num: Number of total block number.
  * @param  block_size: Block size.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_GetCapacity_FS(uint8_t lun, uint32_t *block_num, uint16_t *block_size)
{
  /* USER CODE BEGIN 3 */
  if ((STORAGE_SD_CheckLun((u8)lun) != USBD_OK) || (block_num == NULL) || (block_size == NULL))
  {
    return (USBD_FAIL);
  }

  if (STORAGE_SD_EnsureInit() == 0U)
  {
    return (USBD_FAIL);
  }

  *block_num  = s_sd_card_info.LogBlockNbr;
  *block_size = (u16)s_sd_card_info.LogBlockSize;
  return (USBD_OK);
  /* USER CODE END 3 */
}

/**
  * @brief   Checks whether the medium is ready.
  * @param  lun:  Logical unit number.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_IsReady_FS(uint8_t lun)
{
  /* USER CODE BEGIN 4 */
  if (STORAGE_SD_CheckLun((u8)lun) != USBD_OK)
  {
    return (USBD_FAIL);
  }

  if (STORAGE_SD_EnsureInit() == 0U)
  {
    return (USBD_FAIL);
  }

  return (STORAGE_SD_WaitReady(STORAGE_SD_TIMEOUT_MS) != 0U) ? (USBD_OK) : (USBD_FAIL);
  /* USER CODE END 4 */
}

/**
  * @brief  Checks whether the medium is write protected.
  * @param  lun: Logical unit number.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_IsWriteProtected_FS(uint8_t lun)
{
  /* USER CODE BEGIN 5 */
  if (STORAGE_SD_CheckLun((u8)lun) != USBD_OK)
  {
    return (USBD_FAIL);
  }

#if (STORAGE_SD_WRITE_PROTECTED != 0U)
  return (USBD_FAIL);
#else
  return (USBD_OK);
#endif
  /* USER CODE END 5 */
}

/**
  * @brief  Reads data from the medium.
  * @param  lun: Logical unit number.
  * @param  buf: data buffer.
  * @param  blk_addr: Logical block address.
  * @param  blk_len: Blocks number.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_Read_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
  /* USER CODE BEGIN 6 */
  if ((STORAGE_SD_CheckLun((u8)lun) != USBD_OK) || (buf == NULL))
  {
    return (USBD_FAIL);
  }

  return STORAGE_SD_ReadBlocks((u8 *)buf, (u32)blk_addr, (u16)blk_len);
  /* USER CODE END 6 */
}

/**
  * @brief  Writes data into the medium.
  * @param  lun: Logical unit number.
  * @param  buf: data buffer.
  * @param  blk_addr: Logical block address.
  * @param  blk_len: Blocks number.
  * @retval USBD_OK if all operations are OK else USBD_FAIL
  */
int8_t STORAGE_Write_FS(uint8_t lun, uint8_t *buf, uint32_t blk_addr, uint16_t blk_len)
{
  /* USER CODE BEGIN 7 */
  if ((STORAGE_SD_CheckLun((u8)lun) != USBD_OK) || (buf == NULL))
  {
    return (USBD_FAIL);
  }

#if (STORAGE_SD_WRITE_PROTECTED != 0U)
  return (USBD_FAIL);
#else
  return STORAGE_SD_WriteBlocks((u8 *)buf, (u32)blk_addr, (u16)blk_len);
#endif
  /* USER CODE END 7 */
}

/**
  * @brief  Returns the Max Supported LUNs.
  * @param  None
  * @retval Lun(s) number.
  */
int8_t STORAGE_GetMaxLun_FS(void)
{
  /* USER CODE BEGIN 8 */
  return (STORAGE_LUN_NBR - 1);
  /* USER CODE END 8 */
}

/* USER CODE BEGIN PRIVATE_FUNCTIONS_IMPLEMENTATION */
static int8_t STORAGE_SD_CheckLun(u8 lun)
{
  return (lun < STORAGE_LUN_NBR) ? (USBD_OK) : (USBD_FAIL);
}

static u8 STORAGE_SD_EnsureInit(void)
{
  if (BSP_SD_IsDetected() != SD_PRESENT)
  {
    s_sd_initialized = 0U;
    return 0U;
  }

  if (s_sd_initialized != 0U)
  {
    return (STORAGE_SD_WaitReady(STORAGE_SD_TIMEOUT_MS) != 0U) ? 1U : 0U;
  }

  if (BSP_SD_Init() != MSD_OK)
  {
    s_sd_initialized = 0U;
    return 0U;
  }

  if (STORAGE_SD_WaitReady(STORAGE_SD_TIMEOUT_MS) == 0U)
  {
    s_sd_initialized = 0U;
    return 0U;
  }

  if (STORAGE_SD_RefreshInfo() == 0U)
  {
    s_sd_initialized = 0U;
    return 0U;
  }

  s_sd_initialized = 1U;
  return 1U;
}

static u8 STORAGE_SD_RefreshInfo(void)
{
  BSP_SD_GetCardInfo(&s_sd_card_info);

  if ((s_sd_card_info.LogBlockNbr == 0U) ||
      (s_sd_card_info.LogBlockSize != STORAGE_SD_BLOCK_SIZE))
  {
    return 0U;
  }

  return 1U;
}

static u8 STORAGE_SD_WaitReady(u32 timeout_ms)
{
  u32 start_tick;

  start_tick = HAL_GetTick();
  do
  {
    if (BSP_SD_GetCardState() == SD_TRANSFER_OK)
    {
      return 1U;
    }
  } while ((HAL_GetTick() - start_tick) < timeout_ms);

  return 0U;
}

static u8 STORAGE_SD_IsAligned(const void *ptr)
{
  return ((((u32)ptr) & 0x3U) == 0U) ? 1U : 0U;
}

static int8_t STORAGE_SD_ReadBlocks(u8 *buf, u32 blk_addr, u16 blk_len)
{
  u32 i;
  u8 *scratch;

  if (blk_len == 0U)
  {
    return (USBD_OK);
  }

  if (STORAGE_SD_EnsureInit() == 0U)
  {
    return (USBD_FAIL);
  }

  if ((blk_addr >= s_sd_card_info.LogBlockNbr) ||
      ((u32)blk_len > (s_sd_card_info.LogBlockNbr - blk_addr)))
  {
    return (USBD_FAIL);
  }

  if (STORAGE_SD_WaitReady(STORAGE_SD_TIMEOUT_MS) == 0U)
  {
    return (USBD_FAIL);
  }

  if (STORAGE_SD_IsAligned(buf) != 0U)
  {
    if (BSP_SD_ReadBlocks((u32 *)buf, blk_addr, (u32)blk_len, STORAGE_SD_TIMEOUT_MS) != MSD_OK)
    {
      return (USBD_FAIL);
    }

    return (STORAGE_SD_WaitReady(STORAGE_SD_TIMEOUT_MS) != 0U) ? (USBD_OK) : (USBD_FAIL);
  }

  scratch = (u8 *)s_sd_scratch_word;
  for (i = 0U; i < (u32)blk_len; i++)
  {
    if (BSP_SD_ReadBlocks(s_sd_scratch_word, blk_addr + i, 1U, STORAGE_SD_TIMEOUT_MS) != MSD_OK)
    {
      return (USBD_FAIL);
    }
    if (STORAGE_SD_WaitReady(STORAGE_SD_TIMEOUT_MS) == 0U)
    {
      return (USBD_FAIL);
    }
    (void)memcpy(&buf[i * STORAGE_SD_BLOCK_SIZE], scratch, STORAGE_SD_BLOCK_SIZE);
  }

  return (USBD_OK);
}

static int8_t STORAGE_SD_WriteBlocks(u8 *buf, u32 blk_addr, u16 blk_len)
{
  u32 i;
  u8 *scratch;

  if (blk_len == 0U)
  {
    return (USBD_OK);
  }

  if (STORAGE_SD_EnsureInit() == 0U)
  {
    return (USBD_FAIL);
  }

  if ((blk_addr >= s_sd_card_info.LogBlockNbr) ||
      ((u32)blk_len > (s_sd_card_info.LogBlockNbr - blk_addr)))
  {
    return (USBD_FAIL);
  }

  if (STORAGE_SD_WaitReady(STORAGE_SD_TIMEOUT_MS) == 0U)
  {
    return (USBD_FAIL);
  }

  if (STORAGE_SD_IsAligned(buf) != 0U)
  {
    if (BSP_SD_WriteBlocks((u32 *)buf, blk_addr, (u32)blk_len, STORAGE_SD_TIMEOUT_MS) != MSD_OK)
    {
      return (USBD_FAIL);
    }

    return (STORAGE_SD_WaitReady(STORAGE_SD_TIMEOUT_MS) != 0U) ? (USBD_OK) : (USBD_FAIL);
  }

  scratch = (u8 *)s_sd_scratch_word;
  for (i = 0U; i < (u32)blk_len; i++)
  {
    (void)memcpy(scratch, &buf[i * STORAGE_SD_BLOCK_SIZE], STORAGE_SD_BLOCK_SIZE);
    if (BSP_SD_WriteBlocks(s_sd_scratch_word, blk_addr + i, 1U, STORAGE_SD_TIMEOUT_MS) != MSD_OK)
    {
      return (USBD_FAIL);
    }
    if (STORAGE_SD_WaitReady(STORAGE_SD_TIMEOUT_MS) == 0U)
    {
      return (USBD_FAIL);
    }
  }

  return (USBD_OK);
}

/* USER CODE END PRIVATE_FUNCTIONS_IMPLEMENTATION */

/**
  * @}
  */

/**
  * @}
  */

