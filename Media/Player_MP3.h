#ifndef PLAYER_MP3_H
#define PLAYER_MP3_H

#include "Player_Codec.h"

typedef struct Player_MP3_Stats
{
  u32 sync_miss_count;
  u32 decode_error_count;
  u32 main_data_underflow_count;
  u32 format_mismatch_count;
  u32 recovery_count;
  u32 concealed_frame_count;
  s32 last_decode_error;
  u32 last_bad_sample_rate;
  u32 last_bad_output_samps;
  u8 last_bad_channels;
} Player_MP3_Stats_t;

int Player_MP3_Open(FIL *file, Player_Codec_Handle_t *codec, Player_Info_t *info);
void Player_MP3_GetStats(Player_MP3_Stats_t *stats);
void Player_MP3_ResetStats(void);

#endif
