#ifndef PLAYER_H
#define PLAYER_H

#include "stm32f4xx.h"                  // Device header
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"
#include "sys.h"

typedef struct Wave_Infomation_t {
  uint32_t WAV_Second;
  uint32_t Sample_Rate;
  int Channels;
  int deepth;
}Wave_Infomation;

extern uint8_t play_buf1[1024];
extern uint8_t play_buf2[1024];

void PlayerStart(FIL* audio_file);


#endif
