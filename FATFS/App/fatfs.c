/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
#include "fatfs.h"

uint8_t retSD;    /* Return value for SD */
char SDPath[4];   /* SD logical drive path */
FATFS SDFatFS;    /* File system object for SD logical drive */
FIL SDFile;       /* File object for SD */

/* USER CODE BEGIN Variables */
#if FATFS_LOG_ENABLE
#include <stdarg.h>
#include <stdio.h>
#include "System_Status.h"
#endif

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the SD driver ###########################*/
  retSD = FATFS_LinkDriver(&SD_Driver, SDPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  return 0;
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */
#if FATFS_LOG_ENABLE
#ifndef FATFS_LOG_BUFFER_SIZE
#define FATFS_LOG_BUFFER_SIZE 160U
#endif

void FATFS_LogPrint(const char *fmt, ...)
{
  char msg[FATFS_LOG_BUFFER_SIZE];
  va_list args;
  int len;

  if (fmt == NULL || System_DebugLogEnabled(SYSTEM_DEBUG_LOG_FATFS) == 0U)
  {
    return;
  }

  va_start(args, fmt);
  len = vsnprintf(msg, sizeof(msg), fmt, args);
  va_end(args);

  if (len < 0)
  {
    return;
  }
  if (len >= (int)sizeof(msg))
  {
    len = (int)sizeof(msg) - 1;
  }

  System_DebugLogPrint(SYSTEM_DEBUG_LOG_FATFS, "%s\r\n", msg);
}
#endif

/* USER CODE END Application */
