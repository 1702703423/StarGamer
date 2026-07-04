#include "Player.h"
#include "Malloc.h"
#include "shell.h"
#include "shell_support.h"
#include "System_Status.h"
#include "FreeRTOS.h"
#include "task.h"
#include "i2s.h"

extern I2S_HandleTypeDef hi2s3;
extern SD_HandleTypeDef hsd;
extern TIM_HandleTypeDef htim7;
extern Shell shell;

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
audio_play_info_t apif;

/* this code used for testing and need to be move to other file */

typedef enum WAV_RES_t  {
  WAV_OK = 0,WAV_SAMPLE,ER_WAV_FILE,ER_NOT_WAV,ER_WAV_CHANNEL,ER_WAV_PCM,ER_WAV_DESTORYED,ER_WAV_RIFF,ER_WAV_FMT,
  ER_WAV_FORMAT,ER_WAV_CHANNELS,ER_WAV_DEPTH,ER_WAV_DATA,ER_WAV_NOT_SUPPORT
}WAV_RES;

#define RIFF_CHUNK_SIZE_BYTE  12

#define EVENT_HALF_DMA (1U << 0)
#define EVENT_OVER_DMA (1U << 1)
#define EVENT_STOP_PLAYER (1U << 2)
#define EVENT_QUIT (1U << 3)
#define EVENT_DMA_EMPTY (EVENT_HALF_DMA | EVENT_OVER_DMA)

uint32_t WavTaskBuffer[ 4096 ];
static osThreadId_t WavTaskHandle;
osStaticThreadDef_t WavTaskControlBlock;
const osThreadAttr_t WavTaskAttr = {
  .name = "WaveDataTransmitTask",
  .cb_mem = &WavTaskControlBlock,
  .cb_size = sizeof(WavTaskControlBlock),
  .stack_mem = &WavTaskBuffer[0],
  .stack_size = sizeof(WavTaskBuffer),
  .priority = (osPriority_t) osPriorityHigh2,
};
#define WAV_PLAY_BUFF_SIZE 8192U
__ALIGNED(4) static uint8_t wav_play_buf[WAV_PLAY_BUFF_SIZE];

int WavePlayTaskStart(FIL* file,Wave_Infomation* Wave_Infomation);

static int player_fill_dma_buffer(FIL *file, uint8_t dma_index)
{
  UINT br;
  FRESULT fr;

  fr = f_read(file, wav_play_buf, WAV_PLAY_BUFF_SIZE, &br);
  if(fr != FR_OK)
  {
    shellPrintEndLine(&shell,"TASK_ERROR:WAVE_FILE\r\n");
    return -1;
  }

  if(audio_card_write_to_buffer(dma_index, wav_play_buf, br) != 0)
  {
    return -1;
  }

  if((br < WAV_PLAY_BUFF_SIZE) || f_eof(file))
  {
    return 1;
  }

  return 0;
}

static int player_service_dma_buffer(FIL *file, uint8_t dma_index, uint8_t *drain_events)
{
  int fill_res;

  if(*drain_events > 0U)
  {
    if(audio_card_write_to_buffer(dma_index, NULL, 0) != 0)
    {
      return -1;
    }

    (*drain_events)--;
    return (*drain_events == 0U) ? 1 : 0;
  }

  fill_res = player_fill_dma_buffer(file, dma_index);
  if(fill_res < 0)
  {
    return -1;
  }

  if(fill_res > 0)
  {
    *drain_events = 2U;
  }

  return 0;
}

static void player_exit(FIL *file)
{
  osThreadId_t tid = WavTaskHandle;

  WavTaskHandle = NULL;
  audio_card_register_tx(NULL);
  audio_card_close();
  f_close(file);

  if(tid != NULL)
  {
    SysUnregisterThread(tid);
  }

  osThreadExit();
}

void WavePlayTaskStop(osThreadId_t tid)
{
  if(tid == WavTaskHandle)
  {
    WavTaskHandle = NULL;
  }

  audio_card_register_tx(NULL);
  audio_card_close();

  if(tid != NULL)
  {
    SysUnregisterThread(tid);
    vTaskDelete((TaskHandle_t)tid);
  }
}

void PlayerStart(FIL* audio_file)                                                                                                                                                                    
{
  Wave_Infomation WI;
	
	int res = WavePlayTaskStart(audio_file,&WI);
	shellPrintEndLine(&shell,"WavePlayTaskCreatCode:%d\r\n",res);
  if(res == WAV_OK)
  {
		shellPrintEndLine(&shell,"SampleRate:%d\r\n",WI.Sample_Rate);
    shellPrintEndLine(&shell,"Channels:%d\r\n",WI.Channels);
    shellPrintEndLine(&shell,"deepth:%d\r\n",WI.deepth);
    shellPrintEndLine(&shell,"WI.WAV_Second:%d\r\n",WI.WAV_Second);
  }
}

void WaveDataTransmitTask(void *argument)
{
  uint32_t flag;
	FIL* file = (FIL* )argument;
  int fill_res;
  uint8_t drain_events = 0U;

  fill_res = player_fill_dma_buffer(file, 0U);
  if(fill_res < 0)
  {
		player_exit(file);
  }

  if(fill_res == 0)
  {
    fill_res = player_fill_dma_buffer(file, 1U);
    if(fill_res < 0)
    {
      player_exit(file);
    }
  }

  if(fill_res > 0)
  {
    drain_events = 2U;
  }

  if(audio_card_start() != 0)
  {
    shellPrintEndLine(&shell,"TASK_ERROR:AUDIO_START\r\n");
    player_exit(file);
  }

  while(1)
  {
    flag = osThreadFlagsWait(EVENT_DMA_EMPTY,osFlagsWaitAny,20000);
    if(flag & osFlagsError)
    {
      shellPrintEndLine(&shell,"WaitEventError\r\n");
			player_exit(file);
    }

    if((flag & EVENT_DMA_EMPTY) == EVENT_DMA_EMPTY)
    {
      fill_res = player_service_dma_buffer(file, audio_card_get_idle_buffer_index(), &drain_events);
      if(fill_res != 0)
      {
        shellPrintEndLine(&shell,"PlayOver\r\n");
        player_exit(file);
      }
    }
    else if(flag & EVENT_HALF_DMA)
    {
      fill_res = player_service_dma_buffer(file, 0U, &drain_events);
      if(fill_res != 0)
      {
        shellPrintEndLine(&shell,"PlayOver\r\n");
				player_exit(file);
      }
    }
    else if(flag & EVENT_OVER_DMA)
    {
      fill_res = player_service_dma_buffer(file, 1U, &drain_events);
      if(fill_res != 0)
      {
        shellPrintEndLine(&shell,"PlayOver\r\n");
        player_exit(file);
      }
    }
  }
}

void audio_feeder()
{
	uint8_t dma_index = audio_card_get_idle_buffer_index();

  if(WavTaskHandle == NULL)
  {
    return;
  }

  if(osThreadGetState(WavTaskHandle) == osThreadTerminated)
  {
    WavTaskHandle = NULL;
    audio_card_register_tx(NULL);
    return;
  }

	if(dma_index == 0U)
	{
		osThreadFlagsSet(WavTaskHandle,EVENT_HALF_DMA);
	}
	else
	{
		osThreadFlagsSet(WavTaskHandle,EVENT_OVER_DMA);
	}
}

int WavePlayTaskStart(FIL* file,Wave_Infomation* Wave_Infomation)
{
	
  FRESULT fr;
  uint32_t br;
  __ALIGNED(64) uint8_t buffer[64];
  uint32_t chunk_size;

  fr = f_read(file, buffer, 12, &br);
  if (fr != FR_OK)
    return ER_WAV_FILE;
  if (br < 12)
    return ER_WAV_DESTORYED;

  if (memcmp(&buffer[0], "RIFF", 4) != 0)
    return ER_WAV_RIFF;

  if (memcmp(&buffer[8], "WAVE", 4) != 0)
    return ER_WAV_FORMAT;

  while (1)
  {
    fr = f_read(file, buffer, 8, &br);
    if (fr != FR_OK || br < 8)
      return ER_WAV_FILE;

    chunk_size = buffer[4] | (buffer[5] << 8) |
                 (buffer[6] << 16) | (buffer[7] << 24);

    if (memcmp(buffer, "fmt ", 4) == 0)
    {
      break;
    }

    if (f_lseek(file, f_tell(file) + chunk_size) != FR_OK)
    {
      return ER_WAV_FILE;
    }
  }

  if (chunk_size < 16)
    return ER_WAV_FMT;

  fr = f_read(file, buffer, (chunk_size < 64) ? chunk_size : 64, &br);
  if (fr != FR_OK || br < 16)
    return ER_WAV_FILE;

  // Audio Format 
  uint16_t audio_format = buffer[0] | (buffer[1] << 8);
  if (audio_format != 1)
    return ER_WAV_PCM;

  // Channels 
  Wave_Infomation->Channels = buffer[2] | (buffer[3] << 8);
  if (Wave_Infomation->Channels == 0 || Wave_Infomation->Channels > 2)
  {
    return ER_WAV_CHANNELS;
  }

  // Sample Rate 
  Wave_Infomation->Sample_Rate = buffer[4] | (buffer[5] << 8) |
                                 (buffer[6] << 16) | (buffer[7] << 24);

  // Byte Rate 
  uint32_t byte_rate = buffer[8] | (buffer[9] << 8) |
                       (buffer[10] << 16) | (buffer[11] << 24);

  // Block Align 
  // uint16_t block_align = buffer[12] | (buffer[13] << 8);

  // Bits Per Sample 
  Wave_Infomation->deepth = buffer[14] | (buffer[15] << 8);
  if (Wave_Infomation->deepth != 8 && Wave_Infomation->deepth != 16)
  {
    return ER_WAV_DEPTH;
  }

  if (chunk_size > 16)
  {
    if (f_lseek(file, f_tell(file) + (chunk_size - 16)) != FR_OK)
    {
      return ER_WAV_FILE;
    }
  }

  uint32_t data_size = 0;
  while (1)
  {
    fr = f_read(file, buffer, 8, &br);
    if (fr != FR_OK || br < 8)
      return ER_WAV_DATA;

    chunk_size = buffer[4] | (buffer[5] << 8) |
                 (buffer[6] << 16) | (buffer[7] << 24);

    if (memcmp(buffer, "data", 4) == 0)
    {
      data_size = chunk_size;
      break; 
    }

    if (f_lseek(file, f_tell(file) + chunk_size) != FR_OK)
    {
      return ER_WAV_FILE;
    }
  }

  if (byte_rate > 0)
  {
    Wave_Infomation->WAV_Second = data_size / byte_rate;
  }
  else
  {
    Wave_Infomation->WAV_Second = 0;
  }
	
	audio_card_init(50);
  if(audio_card_open_prepare(Wave_Infomation->Sample_Rate,Wave_Infomation->deepth,Wave_Infomation->Channels,WAV_PLAY_BUFF_SIZE) != 0)
  {
    return ER_WAV_NOT_SUPPORT;
  }
	audio_card_register_tx(audio_feeder);
	WavTaskHandle = SysCreateThread(WaveDataTransmitTask,file,&WavTaskAttr,0);
  if(WavTaskHandle == NULL)
  {
    audio_card_close();
    return ER_WAV_FILE;
  }

  return WAV_OK;
}


