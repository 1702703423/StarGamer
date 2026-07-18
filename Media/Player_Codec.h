#ifndef PLAYER_CODEC_H
#define PLAYER_CODEC_H

#include "Player.h"

typedef enum
{
  PLAYER_CODEC_READ_OK = 0,
  PLAYER_CODEC_READ_EOF = 1,
  PLAYER_CODEC_ERROR = -1,
  PLAYER_CODEC_FORMAT_ERROR = -2,
  PLAYER_CODEC_MEMORY_ERROR = -3,
  PLAYER_CODEC_UNSUPPORTED = -4
} Player_Codec_Result_t;

typedef int (*Player_Codec_Read_t)(void *context, const u8 **pcm, u32 *pcm_bytes);
typedef void (*Player_Codec_Close_t)(void *context);

typedef struct Player_Codec_Handle
{
  void *context;
  Player_Codec_Read_t read_pcm;
  Player_Codec_Close_t close;
  u32 package_size;
} Player_Codec_Handle_t;

#endif
