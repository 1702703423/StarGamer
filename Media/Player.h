#ifndef PLAYER_H
#define PLAYER_H

#include "stm32f4xx.h"                  // Device header
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"
#include "sys.h"

typedef struct Wave_Infomation_t {
  u32 WAV_Second;
  u32 Sample_Rate;
  int Channels;
  int deepth;
}Wave_Infomation;

typedef enum
{
  PLAYER_STATE_IDLE = 0,
  PLAYER_STATE_STARTING,
  PLAYER_STATE_PLAYING,
  PLAYER_STATE_PAUSED,
  PLAYER_STATE_STOPPING,
  PLAYER_STATE_ERROR
} Player_State_t;

extern uint8_t play_buf1[1024];
extern uint8_t play_buf2[1024];

int PlayerStart(const TCHAR *audio_path);
int PlayerPause(void);
int PlayerResume(void);
int PlayerTogglePause(void);
int PlayerStop(void);
void PlayerCleanup(void);
u8 PlayerIsBusy(void);
Player_State_t PlayerGetState(void);
int PlayerGetInfo(Wave_Infomation *info);
const TCHAR *PlayerGetCurrentPath(void);
void WavePlayTaskStop(osThreadId_t tid);


#endif
