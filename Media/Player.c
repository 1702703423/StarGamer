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
  ER_WAV_FORMAT,ER_WAV_CHANNELS,ER_WAV_DEPTH,ER_WAV_DATA,ER_WAV_NOT_SUPPORT,ER_WAV_MEMORY
}WAV_RES;

#define RIFF_CHUNK_SIZE_BYTE  12

#define EVENT_HALF_DMA (1U << 0)
#define EVENT_OVER_DMA (1U << 1)
#define EVENT_STOP_PLAYER (1U << 2)
#define EVENT_QUIT (1U << 3)
#define EVENT_PAUSE_PLAYER (1U << 4)
#define EVENT_RESUME_PLAYER (1U << 5)
#define EVENT_DMA_EMPTY (EVENT_HALF_DMA | EVENT_OVER_DMA)
#define EVENT_PLAYER_WAIT (EVENT_DMA_EMPTY | EVENT_STOP_PLAYER | EVENT_PAUSE_PLAYER)
#define PLAYER_PATH_SIZE 256U

#define WAV_THREAD_STACK_SIZE (2048U * sizeof(u32))
#define WAV_CLEANUP_DELAY_TICKS 2U
static osThreadId_t WavTaskHandle;
static FIL WavFile;
static u8 WavFileOpened = 0U;
static volatile Player_State_t s_player_state = PLAYER_STATE_IDLE;
static volatile u32 WavTaskExitTick;
static volatile u32 WavTaskStackSpace;
static Wave_Infomation s_player_info;
static TCHAR s_player_path[PLAYER_PATH_SIZE];
osStaticThreadDef_t WavTaskControlBlock;
osThreadAttr_t WavTaskAttr = {
  .name = "WaveDataTransmitTask",
  .cb_mem = &WavTaskControlBlock,
  .cb_size = sizeof(WavTaskControlBlock),
  .stack_mem = NULL,
  .stack_size = WAV_THREAD_STACK_SIZE,
  .priority = (osPriority_t) osPriorityHigh2,
};
#define WAV_PLAY_BUFF_SIZE 16384U
/* Keep this SDIO DMA target allocated for the lifetime of the firmware. */
static u8 *wav_play_buf;

int WavePlayTaskStart(FIL* file,Wave_Infomation* Wave_Infomation);
static void player_free_stack(void);

static u8 player_copy_path(TCHAR *dst, u32 dst_size, const TCHAR *src)
{
  u32 i;

  if(dst == NULL || dst_size == 0U || src == NULL)
  {
    return 0U;
  }

  for(i = 0U; i < (dst_size - 1U); i++)
  {
    dst[i] = src[i];
    if(src[i] == '\0')
    {
      return 1U;
    }
  }

  dst[dst_size - 1U] = '\0';
  return 0U;
}

static void player_free_stack(void)
{
  if(WavTaskAttr.stack_mem != NULL)
  {
    sys_thread_stack_free(WavTaskAttr.stack_mem);
    WavTaskAttr.stack_mem = NULL;
  }
}

static void player_close_file(FIL *file)
{
  if(WavFileOpened != 0U)
  {
    f_close(file);
    WavFileOpened = 0U;
  }
}

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

  s_player_state = PLAYER_STATE_IDLE;
  audio_card_register_tx(NULL);
  audio_card_close();
  player_close_file(file);

  if(tid != NULL)
  {
    SysUnregisterThread(tid);
  }

  WavTaskStackSpace = (u32)uxTaskGetStackHighWaterMark(NULL) * (u32)sizeof(StackType_t);
  __DMB();
  WavTaskExitTick = xTaskGetTickCount();
  __DMB();
  osThreadExit();
}

static void player_pause_wait(FIL *file)
{
  u32 flag;

  if(audio_card_pause() != 0)
  {
    shellPrintEndLine(&shell,"TASK_ERROR:AUDIO_PAUSE\r\n");
    player_exit(file);
  }

  s_player_state = PLAYER_STATE_PAUSED;

  while(1)
  {
    flag = osThreadFlagsWait(EVENT_RESUME_PLAYER | EVENT_STOP_PLAYER, osFlagsWaitAny, osWaitForever);
    if(flag & osFlagsError)
    {
      shellPrintEndLine(&shell,"PauseWaitError\r\n");
      player_exit(file);
    }

    if(flag & EVENT_STOP_PLAYER)
    {
      player_exit(file);
    }

    if(flag & EVENT_RESUME_PLAYER)
    {
      if(audio_card_resume() != 0)
      {
        shellPrintEndLine(&shell,"TASK_ERROR:AUDIO_RESUME\r\n");
        player_exit(file);
      }

      s_player_state = PLAYER_STATE_PLAYING;
      return;
    }
  }
}

void WavePlayTaskStop(osThreadId_t tid)
{
  u8 close_file = 0U;

  if(tid == WavTaskHandle)
  {
    WavTaskHandle = NULL;
    WavTaskExitTick = 0U;
    WavTaskStackSpace = 0U;
    close_file = 1U;
  }

  s_player_state = PLAYER_STATE_IDLE;
  audio_card_register_tx(NULL);
  audio_card_close();

  if(tid != NULL)
  {
    SysUnregisterThread(tid);
    vTaskDelete((TaskHandle_t)tid);
  }

  if(close_file != 0U)
  {
    player_close_file(&WavFile);
    player_free_stack();
  }
}

int PlayerStart(const TCHAR *audio_path)
{
  Wave_Infomation WI;
  FRESULT fr;
  int res;

  PlayerCleanup();
  if(audio_path == NULL)
  {
    shellPrintEndLine(&shell,"PlayerStart:NULL_PATH\r\n");
    return OPERATE_FAILED;
  }

  if(WavTaskHandle != NULL)
  {
    shellPrintEndLine(&shell,"PlayerStart:BUSY\r\n");
    return OPERATE_FAILED;
  }

  if(player_copy_path(s_player_path, PLAYER_PATH_SIZE, audio_path) == 0U)
  {
    shellPrintEndLine(&shell,"PlayerStart:PATH_TOO_LONG\r\n");
    return OPERATE_FAILED;
  }

  player_close_file(&WavFile);
  memset(&WI, 0, sizeof(WI));
  memset(&s_player_info, 0, sizeof(s_player_info));
  WavTaskStackSpace = 0U;
  WavTaskExitTick = 0U;
  s_player_state = PLAYER_STATE_STARTING;

  fr = f_open(&WavFile,audio_path,FA_READ);
  if(fr != FR_OK)
  {
    shellPrintEndLine(&shell,"PlayerStart:f_open:%d\r\n",fr);
    s_player_state = PLAYER_STATE_ERROR;
    return OPERATE_FAILED;
  }
  WavFileOpened = 1U;

	res = WavePlayTaskStart(&WavFile,&WI);
	shellPrintEndLine(&shell,"WavePlayTaskCreatCode:%d\r\n",res);
  if(res != WAV_OK)
  {
    player_close_file(&WavFile);
    s_player_state = PLAYER_STATE_ERROR;
    return OPERATE_FAILED;
  }

  if(res == WAV_OK)
  {
    s_player_info = WI;
		shellPrintEndLine(&shell,"SampleRate:%d\r\n",WI.Sample_Rate);
    shellPrintEndLine(&shell,"Channels:%d\r\n",WI.Channels);
    shellPrintEndLine(&shell,"deepth:%d\r\n",WI.deepth);
    shellPrintEndLine(&shell,"WI.WAV_Second:%d\r\n",WI.WAV_Second);
  }

  return SYSTEM_STATUS_OK;
}

void PlayerCleanup(void)
{
  u32 now;

  if(WavTaskHandle == NULL || WavTaskExitTick == 0U)
  {
    return;
  }

  now = (u32)xTaskGetTickCount();
  if((now - WavTaskExitTick) < WAV_CLEANUP_DELAY_TICKS)
  {
    return;
  }

  player_free_stack();
  WavTaskHandle = NULL;
  WavTaskExitTick = 0U;
}

int PlayerPause(void)
{
  u32 flags;

  PlayerCleanup();
  if(WavTaskHandle == NULL || WavTaskExitTick != 0U)
  {
    return OPERATE_FAILED;
  }

  if(s_player_state == PLAYER_STATE_PAUSED)
  {
    return SYSTEM_STATUS_OK;
  }

  flags = osThreadFlagsSet(WavTaskHandle, EVENT_PAUSE_PLAYER);
  return (flags & osFlagsError) ? OPERATE_FAILED : SYSTEM_STATUS_OK;
}

int PlayerResume(void)
{
  u32 flags;

  PlayerCleanup();
  if(WavTaskHandle == NULL || WavTaskExitTick != 0U)
  {
    return OPERATE_FAILED;
  }

  if(s_player_state != PLAYER_STATE_PAUSED)
  {
    return SYSTEM_STATUS_OK;
  }

  flags = osThreadFlagsSet(WavTaskHandle, EVENT_RESUME_PLAYER);
  return (flags & osFlagsError) ? OPERATE_FAILED : SYSTEM_STATUS_OK;
}

int PlayerTogglePause(void)
{
  if(s_player_state == PLAYER_STATE_PAUSED)
  {
    return PlayerResume();
  }

  return PlayerPause();
}

int PlayerStop(void)
{
  u32 flags;

  PlayerCleanup();
  if(WavTaskHandle == NULL || WavTaskExitTick != 0U)
  {
    s_player_state = PLAYER_STATE_IDLE;
    return SYSTEM_STATUS_OK;
  }

  s_player_state = PLAYER_STATE_STOPPING;
  flags = osThreadFlagsSet(WavTaskHandle, EVENT_STOP_PLAYER);
  return (flags & osFlagsError) ? OPERATE_FAILED : SYSTEM_STATUS_OK;
}

u8 PlayerIsBusy(void)
{
  PlayerCleanup();
  return (WavTaskHandle != NULL) ? 1U : 0U;
}

Player_State_t PlayerGetState(void)
{
  return s_player_state;
}

int PlayerGetInfo(Wave_Infomation *info)
{
  if(info == NULL)
  {
    return OPERATE_FAILED;
  }

  *info = s_player_info;
  return SYSTEM_STATUS_OK;
}

const TCHAR *PlayerGetCurrentPath(void)
{
  return s_player_path;
}

void WaveDataTransmitTask(void *argument)
{
  uint32_t flag;
	FIL* file = (FIL* )argument;
  int fill_res;
  uint8_t drain_events = 0U;

  (void)osThreadFlagsClear(EVENT_STOP_PLAYER | EVENT_PAUSE_PLAYER | EVENT_RESUME_PLAYER);

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
  s_player_state = PLAYER_STATE_PLAYING;

  while(1)
  {
    flag = osThreadFlagsWait(EVENT_PLAYER_WAIT,osFlagsWaitAny,20000);
    if(flag & osFlagsError)
    {
      shellPrintEndLine(&shell,"WaitEventError\r\n");
			player_exit(file);
    }

    if(flag & EVENT_STOP_PLAYER)
    {
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

    if(flag & EVENT_PAUSE_PLAYER)
    {
      player_pause_wait(file);
    }
  }
}

void audio_feeder()
{
	uint8_t dma_index = audio_card_get_idle_buffer_index();

  if(WavTaskHandle == NULL || WavTaskExitTick != 0U)
  {
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

  if(wav_play_buf == NULL)
  {
    wav_play_buf = (u8 *)sys_malloc(SRAMEX, WAV_PLAY_BUFF_SIZE);
    if(wav_play_buf == NULL)
    {
      shellPrintEndLine(&shell,"WavPlayBufferAllocFailed\r\n");
      return ER_WAV_MEMORY;
    }
  }
	
	audio_card_init(audio_card_get_volume());
  if(audio_card_open_prepare(Wave_Infomation->Sample_Rate,Wave_Infomation->deepth,Wave_Infomation->Channels,WAV_PLAY_BUFF_SIZE) != 0)
  {
    return ER_WAV_NOT_SUPPORT;
  }
	audio_card_register_tx(audio_feeder);
	if (WavTaskAttr.stack_mem == NULL)
	{
		WavTaskAttr.stack_mem = sys_thread_stack_alloc((u32)WavTaskAttr.stack_size);
	}
	if (WavTaskAttr.stack_mem == NULL)
	{
		audio_card_register_tx(NULL);
		audio_card_close();
		return ER_WAV_FILE;
	}
  WavTaskExitTick = 0U;
	WavTaskHandle = SysCreateThread(WaveDataTransmitTask,file,&WavTaskAttr,0);
  if(WavTaskHandle == NULL)
  {
    audio_card_register_tx(NULL);
    audio_card_close();
    player_free_stack();
    return ER_WAV_FILE;
  }

  return WAV_OK;
}


