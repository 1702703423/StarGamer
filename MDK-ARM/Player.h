#ifndef PLAYER_H
#define PLAYER_H

#include "stm32f4xx.h"                  // Device header
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"
#include "Player.h"
#include "sys.h"

typedef struct audio_play_info
{
	FIL* file;
	uint8_t* wav_buffer;
}audio_play_info_t;

void PlayerStart(FIL* audio_file);
void PCM_DataTransmitStop();
void PCM_DataTransmit(u8* pcm_data_addr, u32 length, u32 alignment);
void PCM_DataTransmitSet(u16 samplerate,u16 channel,u8 bits);

#endif
