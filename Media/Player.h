#ifndef PLAYER_H
#define PLAYER_H

#include "stm32f4xx.h"                  // Device header
#include "main.h"
#include "cmsis_os.h"
#include "fatfs.h"
#include "sys.h"

typedef enum
{
  PLAYER_CODEC_WAV = 0,
  PLAYER_CODEC_MP3
} Player_Codec_t;

typedef struct Player_Info
{
  Player_Codec_t codec;
  u32 duration_seconds;
  u32 sample_rate;
  u32 bitrate;
  u8 channels;
  u8 bits_per_sample;
} Player_Info_t;

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
int PlayerGetInfo(Player_Info_t *info);
const TCHAR *PlayerGetCurrentPath(void);
void WavePlayTaskStop(osThreadId_t tid);


#endif
