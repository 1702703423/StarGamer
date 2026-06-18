#include "Player.h"
#include "Malloc.h"

extern DAC_HandleTypeDef hdac;
extern DMA_HandleTypeDef hdma_dac1;
extern SD_HandleTypeDef hsd;
extern TIM_HandleTypeDef htim7;

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
typedef StaticEventGroup_t osStaticEventGroupDef_t;

/* Definitions for WAV_DMA_EVENT */
osEventFlagsId_t WAV_DMA_EVENTHandle;
osStaticEventGroupDef_t WAV_DMA_EVENTControlBlock;
const osEventFlagsAttr_t WAV_DMA_EVENT_attributes = {
  .name = "WAV_DMA_EVENT",
  .cb_mem = &WAV_DMA_EVENTControlBlock,
  .cb_size = sizeof(WAV_DMA_EVENTControlBlock),
};

/* this code used for testing and need to be move to other file */

typedef enum WAV_RES_t  {
  WAV_OK = 0,WAV_SAMPLE,ER_WAV_FILE,ER_NOT_WAV,ER_WAV_CHANNEL,ER_WAV_PCM,ER_WAV_DESTORYED,ER_WAV_RIFF,ER_WAV_FMT,
  ER_WAV_FORMAT,ER_WAV_CHANNELS,ER_WAV_DEPTH,ER_WAV_DATA,ER_WAV_NOT_SUPPORT
}WAV_RES;

typedef struct Wave_Infomation_t {
  uint32_t WAV_Second;
  uint32_t Sample_Rate;
  int Channels;
  int deepth;
}Wave_Infomation;

#define RIFF_CHUNK_SIZE_BYTE  12

#define Sample22050   1904
#define Sample44010   1905

#define EVENT_HALF_DMA (1 << 0)  
#define EVENT_OVER_DMA (1 << 1)  

uint32_t WavTaskBuffer[ 4096 ];
osStaticThreadDef_t WavTaskControlBlock;
const osThreadAttr_t WavTaskAttr = {
  .name = "defaultTask",
  .cb_mem = &WavTaskControlBlock,
  .cb_size = sizeof(WavTaskControlBlock),
  .stack_mem = &WavTaskBuffer[0],
  .stack_size = sizeof(WavTaskBuffer),
  .priority = (osPriority_t) osPriorityHigh2,
};

int WavePlayTaskStart(FIL* file,Wave_Infomation* Wave_Infomation);

void PlayerStart(FIL* audio_file)                                                                                                                                                                    
{
  Wave_Infomation WI;
	
	int res = WavePlayTaskStart(audio_file,&WI);
	printf("WavePlayTaskCreatCode:%d\r\n",res);
  if(res == WAV_OK)
  {
		printf("SampleRate:%d\r\n",WI.Sample_Rate);
    printf("Channels:%d\r\n",WI.Channels);
    printf("deepth:%d\r\n",WI.deepth);
    printf("WI.WAV_Second:%d\r\n",WI.WAV_Second);
  }
}

void PCM_DataTransmitSet(u16 samplerate,u16 channel,u8 bits)
{
//	switch(bits)
//	{
//		case 8:
//			hdac.DMA_Handle1->Init.MemDataAlignment = DMA_MDATAALIGN_BYTE;
//			break;
//	}
//	
//	switch(channel)
//	{
//		case 1:
//			hdac.DMA_Handle1->Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
//			break;
//		case 2:
//			hdac.DMA_Handle1->Init.MemDataAlignment = DMA_MDATAALIGN_WORD;
//			break;
//	}
	hdac.DMA_Handle1->Init.MemDataAlignment = DMA_MDATAALIGN_HALFWORD;
	hdac.DMA_Handle1->Init.Mode = DMA_NORMAL;
	htim7.Init.Period = 1904;
  htim7.Init.Prescaler = 0x01;
	
	if (HAL_TIM_Base_Init(&htim7) != HAL_OK || HAL_DMA_Init(hdac.DMA_Handle1) != HAL_OK)
  {
    Error_Handler();
  }
	
	HAL_TIM_Base_Start(&htim7);
}

void PCM_DataTransmit(u8* pcm_data_addr, u32 length, u32 alignment)
{
	HAL_DAC_Stop_DMA(&hdac, DAC_CHANNEL_1);

	HAL_DAC_Start_DMA(&hdac, DAC_CHANNEL_1,(u32*)pcm_data_addr, length, alignment);
}

void PCM_DataTransmitStop()
{
	 HAL_DAC_Stop_DMA(&hdac,DAC_CHANNEL_1);
   HAL_TIM_Base_Stop(&htim7);
}

void WaveDataTransmitTask(void *argument)
{
  uint32_t flag;
  audio_play_info_t* apif = (audio_play_info_t*)argument;
	uint8_t* WAV_PLAY_BUFF = apif->wav_buffer;
	FIL* file = apif->file;
  UINT br;
  FRESULT fr;
  fr = f_read(file,WAV_PLAY_BUFF,WAV_PLAY_BUFF_SIZE,&br);
  if(fr != FR_OK)
  {
    printf("TASK_ERROR:WAVE_FILE"); //printf need to be removed after testing
    HAL_DAC_Stop_DMA(&hdac,DAC_CHANNEL_1);
    HAL_TIM_Base_Stop(&htim7);
    f_close(file);
		sys_free(1,apif->wav_buffer);
		sys_free(0,apif);
    osThreadExit();
  }
  while(1)
  {
    flag = osEventFlagsWait(WAV_DMA_EVENTHandle,EVENT_HALF_DMA | EVENT_OVER_DMA,osFlagsWaitAny,20000);
    if(flag & osFlagsError)
    {
      printf("WaitEventError\n");
      HAL_DAC_Stop_DMA(&hdac,DAC_CHANNEL_1);
      HAL_TIM_Base_Stop(&htim7);
      f_close(file);
			sys_free(1,apif->wav_buffer);
			sys_free(0,apif);
      osThreadExit();
    }
    if(flag & EVENT_HALF_DMA)
    {
      osEventFlagsClear(WAV_DMA_EVENTHandle,EVENT_HALF_DMA);
			fr = f_read(file,WAV_PLAY_BUFF,WAV_PLAY_BUFF_SIZE / 2,&br);
      if(f_eof(file))
      {
        printf("PlayOver");
        HAL_DAC_Stop_DMA(&hdac,DAC_CHANNEL_1);
        HAL_TIM_Base_Stop(&htim7);
        f_close(file);
				sys_free(1,apif->wav_buffer);
				sys_free(0,apif);
        osThreadExit();
      }
      for(int i = 0;i<WAV_PLAY_BUFF_SIZE / 8;i++)//one channle only -handle
				*((uint32_t*)WAV_PLAY_BUFF + i) ^= 0x8000;
    }
    if(flag & EVENT_OVER_DMA)
    {
      osEventFlagsClear(WAV_DMA_EVENTHandle,EVENT_OVER_DMA);
			fr = f_read(file,WAV_PLAY_BUFF + (WAV_PLAY_BUFF_SIZE / 2),WAV_PLAY_BUFF_SIZE / 2,&br);
      if(f_eof(file))
      {
        printf("PlayOver");
        HAL_DAC_Stop_DMA(&hdac,DAC_CHANNEL_1);
        HAL_TIM_Base_Stop(&htim7);
				sys_free(1,apif->wav_buffer);
				sys_free(0,apif);
        f_close(file);
        osThreadExit();
      }
      for(int i = 0;i<WAV_PLAY_BUFF_SIZE / 8;i++)//one channle only -handle
				*((uint32_t*)WAV_PLAY_BUFF+ WAV_PLAY_BUFF_SIZE / 8 + i) ^= 0x8000;
    }
  }
}

int WavePlayInit(int sample_rate,int channels,uint8_t* buffer)
{
  int32_t sample;
  switch (sample_rate)
  {
  case 22050:
    sample = Sample22050;
    break;
  case 44010:
    sample = Sample44010;
    break;
  default:
    return ER_WAV_NOT_SUPPORT;
  }
	
	hdac.DMA_Handle1->Init.Mode = DMA_CIRCULAR;
  htim7.Init.Period = sample;
  htim7.Init.Prescaler = 0x01;

  if (HAL_TIM_Base_Init(&htim7) != HAL_OK || HAL_DMA_Init(hdac.DMA_Handle1) != HAL_OK)
  {
    Error_Handler();
  }

  HAL_DAC_Start_DMA(&hdac,DAC_CHANNEL_1,(uint32_t*)buffer,WAV_PLAY_BUFF_SIZE / 4,DAC_ALIGN_12B_L);
  HAL_TIM_Base_Start(&htim7);
	
	return 0;
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
  if (Wave_Infomation->deepth != 8 && Wave_Infomation->deepth != 16 &&
      Wave_Infomation->deepth != 24 && Wave_Infomation->deepth != 32)
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
	uint8_t* WAV_PLAY_BUFF = (uint8_t*)sys_malloc(1,WAV_PLAY_BUFF_SIZE);
	audio_play_info_t* apif = (audio_play_info_t*)sys_malloc(0,sizeof(audio_play_info_t));
	apif->file = file;
	apif->wav_buffer = WAV_PLAY_BUFF;
	
	WAV_DMA_EVENTHandle = osEventFlagsNew(&WAV_DMA_EVENT_attributes);

  WavePlayInit(Wave_Infomation->Sample_Rate,Wave_Infomation->Channels,apif->wav_buffer);
  osThreadNew(WaveDataTransmitTask,(void*)apif,&WavTaskAttr);

  return WAV_OK;
}

void HAL_DAC_ConvHalfCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
  osEventFlagsSet(WAV_DMA_EVENTHandle,EVENT_HALF_DMA);
}

void HAL_DAC_ConvCpltCallbackCh1(DAC_HandleTypeDef *hdac)
{
  osEventFlagsSet(WAV_DMA_EVENTHandle,EVENT_OVER_DMA);
}
