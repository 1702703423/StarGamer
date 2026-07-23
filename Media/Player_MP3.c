#include "Player_MP3.h"

#include "Malloc.h"
#include "mp3dec.h"

#include <string.h>

#define PLAYER_MP3_INPUT_SIZE            (5U * 1024U)
#define PLAYER_MP3_REFILL_THRESHOLD      (MAINBUF_SIZE * 2U)
#define PLAYER_MP3_MAX_PCM_SAMPLES       2304U
#define PLAYER_MP3_MAX_RECOVERY_ATTEMPTS 32U
#define PLAYER_MP3_MEM_POOL              SRAMEX

typedef struct Player_MP3_Context
{
  FIL *file;
  HMP3Decoder decoder;
  u8 *input_buffer;
  s16 *pcm_buffer;
  int bytes_left;
  u32 audio_start_offset;
  u32 audio_size;
  u32 read_offset;
  u32 ready_bytes;
  u32 duration_seconds;
  u32 position_base_seconds;
  u32 decoded_pcm_bytes;
  u32 pcm_byte_rate;
  u32 expected_output_samps;
  u32 expected_sample_rate;
  u8 expected_channels;
  u8 eof;
  u8 pcm_ready;
  u8 format_locked;
} Player_MP3_Context_t;

static Player_MP3_Stats_t s_player_mp3_stats;

static u32 player_mp3_estimate_duration(u32 audio_size, u32 bitrate)
{
  u32 seconds;

  if(bitrate == 0U)
  {
    return 0U;
  }

  seconds = (audio_size / bitrate) * 8U;
  seconds += ((audio_size % bitrate) * 8U) / bitrate;
  return seconds;
}

static void player_mp3_count_pcm(Player_MP3_Context_t *mp3, u32 pcm_bytes)
{
  if(mp3 == NULL)
  {
    return;
  }

  if((0xFFFFFFFFUL - mp3->decoded_pcm_bytes) < pcm_bytes)
  {
    mp3->decoded_pcm_bytes = 0xFFFFFFFFUL;
  }
  else
  {
    mp3->decoded_pcm_bytes += pcm_bytes;
  }
}

/* Helix has consumed the compressed frame and cleared PCM for these errors. */
static u8 player_mp3_error_is_concealed(int decode_result)
{
  switch(decode_result)
  {
    case ERR_MP3_MAINDATA_UNDERFLOW:
    case ERR_MP3_INVALID_SCALEFACT:
    case ERR_MP3_INVALID_HUFFCODES:
    case ERR_MP3_INVALID_DEQUANTIZE:
    case ERR_MP3_INVALID_IMDCT:
    case ERR_MP3_INVALID_SUBBAND:
      return 1U;

    default:
      return 0U;
  }
}

static int player_mp3_skip_id3v2(FIL *file, u32 *audio_start_offset)
{
  u8 header[10];
  UINT bytes_read;
  u32 tag_size;
  u32 audio_offset;

  if(f_lseek(file, 0U) != FR_OK ||
     f_read(file, header, sizeof(header), &bytes_read) != FR_OK)
  {
    return PLAYER_CODEC_ERROR;
  }

  if(bytes_read < sizeof(header) || memcmp(header, "ID3", 3U) != 0)
  {
    if(audio_start_offset != NULL)
    {
      *audio_start_offset = 0U;
    }
    return (f_lseek(file, 0U) == FR_OK) ? PLAYER_CODEC_READ_OK : PLAYER_CODEC_ERROR;
  }

  if((header[6] & 0x80U) != 0U || (header[7] & 0x80U) != 0U ||
     (header[8] & 0x80U) != 0U || (header[9] & 0x80U) != 0U)
  {
    return PLAYER_CODEC_FORMAT_ERROR;
  }

  tag_size = ((u32)header[6] << 21) |
             ((u32)header[7] << 14) |
             ((u32)header[8] << 7) |
             (u32)header[9];
  audio_offset = 10U + tag_size;
  if((header[5] & 0x10U) != 0U)
  {
    audio_offset += 10U;
  }

  if((FSIZE_t)audio_offset > f_size(file))
  {
    return PLAYER_CODEC_FORMAT_ERROR;
  }

  if(f_lseek(file, (FSIZE_t)audio_offset) != FR_OK)
  {
    return PLAYER_CODEC_ERROR;
  }

  if(audio_start_offset != NULL)
  {
    *audio_start_offset = audio_offset;
  }
  return PLAYER_CODEC_READ_OK;
}

static int player_mp3_refill(Player_MP3_Context_t *mp3)
{
  UINT bytes_read = 0U;
  u32 retained;
  u32 free_size;

  if(mp3->bytes_left < 0 || (u32)mp3->bytes_left > PLAYER_MP3_INPUT_SIZE ||
     mp3->read_offset > PLAYER_MP3_INPUT_SIZE - (u32)mp3->bytes_left)
  {
    return PLAYER_CODEC_ERROR;
  }
  if((u32)mp3->bytes_left >= PLAYER_MP3_REFILL_THRESHOLD)
  {
    return PLAYER_CODEC_READ_OK;
  }

  retained = (u32)mp3->bytes_left;
  if(retained > 0U)
  {
    memmove(mp3->input_buffer,
            mp3->input_buffer + mp3->read_offset,
            retained);
  }
  mp3->read_offset = 0U;

  free_size = PLAYER_MP3_INPUT_SIZE - retained;
  if(mp3->eof == 0U)
  {
    if(f_read(mp3->file, mp3->input_buffer + retained, free_size, &bytes_read) != FR_OK)
    {
      return PLAYER_CODEC_ERROR;
    }
    if(bytes_read < free_size || f_eof(mp3->file))
    {
      mp3->eof = 1U;
    }
  }

  mp3->bytes_left += (int)bytes_read;
  if(mp3->bytes_left == 0)
  {
    return PLAYER_CODEC_READ_EOF;
  }

  return PLAYER_CODEC_READ_OK;
}

static u8 player_mp3_frame_info_valid(const MP3FrameInfo *frame_info)
{
  if(frame_info->bitsPerSample != 16 ||
     (frame_info->nChans != 1 && frame_info->nChans != 2) ||
     frame_info->samprate <= 0 || frame_info->outputSamps <= 0 ||
     (u32)frame_info->outputSamps > PLAYER_MP3_MAX_PCM_SAMPLES ||
     frame_info->layer != 3 || frame_info->version < 0 || frame_info->version > 2)
  {
    return 0U;
  }

  return 1U;
}

static u8 player_mp3_header_valid(const u8 *frame, u32 available)
{
  u8 version_bits;
  u8 layer_bits;
  u8 bitrate_index;
  u8 sample_rate_index;
  u8 header_size;

  if(frame == NULL || available < 4U || frame[0] != 0xFFU ||
     (frame[1] & 0xE0U) != 0xE0U)
  {
    return 0U;
  }

  version_bits = (u8)((frame[1] >> 3) & 0x03U);
  layer_bits = (u8)((frame[1] >> 1) & 0x03U);
  bitrate_index = (u8)((frame[2] >> 4) & 0x0FU);
  sample_rate_index = (u8)((frame[2] >> 2) & 0x03U);
  header_size = ((frame[1] & 0x01U) != 0U) ? 4U : 6U;

  if(version_bits == 1U || layer_bits != 1U || bitrate_index == 15U ||
     sample_rate_index == 3U || (frame[3] & 0x03U) == 2U ||
     available < (u32)header_size)
  {
    return 0U;
  }

  return 1U;
}

static u8 player_mp3_frame_infos_match(const MP3FrameInfo *left,
                                       const MP3FrameInfo *right)
{
  return (left->samprate == right->samprate &&
          left->nChans == right->nChans &&
          left->outputSamps == right->outputSamps &&
          left->layer == right->layer &&
          left->version == right->version) ? 1U : 0U;
}

static u8 player_mp3_output_matches(Player_MP3_Context_t *mp3,
                                    const MP3FrameInfo *frame_info)
{
  if(player_mp3_frame_info_valid(frame_info) == 0U)
  {
    return 0U;
  }

  if(mp3->format_locked == 0U)
  {
    mp3->expected_output_samps = (u32)frame_info->outputSamps;
    mp3->expected_sample_rate = (u32)frame_info->samprate;
    mp3->expected_channels = (u8)frame_info->nChans;
    mp3->format_locked = 1U;
    return 1U;
  }

  return (mp3->expected_output_samps == (u32)frame_info->outputSamps &&
          mp3->expected_sample_rate == (u32)frame_info->samprate &&
          mp3->expected_channels == (u8)frame_info->nChans) ? 1U : 0U;
}

static void player_mp3_advance(Player_MP3_Context_t *mp3, u32 byte_count)
{
  if(byte_count > (u32)mp3->bytes_left)
  {
    byte_count = (u32)mp3->bytes_left;
  }

  mp3->read_offset += byte_count;
  mp3->bytes_left -= (int)byte_count;
}

static void player_mp3_recover(Player_MP3_Context_t *mp3)
{
  player_mp3_advance(mp3, 1U);
  s_player_mp3_stats.recovery_count++;
}

static void player_mp3_note_bad_format(const MP3FrameInfo *frame_info)
{
  s_player_mp3_stats.format_mismatch_count++;
  s_player_mp3_stats.last_bad_sample_rate =
      (frame_info->samprate > 0) ? (u32)frame_info->samprate : 0U;
  s_player_mp3_stats.last_bad_channels =
      (frame_info->nChans > 0) ? (u8)frame_info->nChans : 0U;
  s_player_mp3_stats.last_bad_output_samps =
      (frame_info->outputSamps > 0) ? (u32)frame_info->outputSamps : 0U;
}

static u32 player_mp3_frame_size(const u8 *frame, const MP3FrameInfo *frame_info)
{
  u32 samples_per_channel;
  u32 frame_size;

  if(frame_info->bitrate <= 0 || frame_info->nChans <= 0 ||
     frame_info->samprate <= 0 || frame_info->outputSamps <= 0)
  {
    return 0U;
  }

  samples_per_channel = (u32)frame_info->outputSamps / (u32)frame_info->nChans;
  frame_size = (samples_per_channel * (u32)frame_info->bitrate) /
               ((u32)frame_info->samprate * 8U);
  if((frame[2] & 0x02U) != 0U)
  {
    frame_size++;
  }

  return frame_size;
}

static u8 player_mp3_find_matching_header(Player_MP3_Context_t *mp3,
                                          const u8 *frame,
                                          u32 available,
                                          const MP3FrameInfo *frame_info)
{
  MP3FrameInfo next_info;
  u32 frame_size;
  u32 search_offset;
  int sync_offset;

  frame_size = player_mp3_frame_size(frame, frame_info);
  if(frame_size >= 4U && frame_size + 4U <= available)
  {
    memset(&next_info, 0, sizeof(next_info));
    if(player_mp3_header_valid(frame + frame_size, available - frame_size) != 0U &&
       MP3GetNextFrameInfo(mp3->decoder, &next_info,
                           (unsigned char *)(frame + frame_size)) == ERR_MP3_NONE &&
       player_mp3_frame_info_valid(&next_info) != 0U &&
       player_mp3_frame_infos_match(frame_info, &next_info) != 0U)
    {
      return 1U;
    }
    return 0U;
  }

  /* Free-bitrate streams have no computable frame size; confirm by scanning. */
  search_offset = 4U;
  while(search_offset + 4U <= available)
  {
    sync_offset = MP3FindSyncWord((unsigned char *)(frame + search_offset),
                                  (int)(available - search_offset));
    if(sync_offset < 0)
    {
      break;
    }
    search_offset += (u32)sync_offset;
    memset(&next_info, 0, sizeof(next_info));
    if(player_mp3_header_valid(frame + search_offset,
                               available - search_offset) != 0U &&
       MP3GetNextFrameInfo(mp3->decoder, &next_info,
                           (unsigned char *)(frame + search_offset)) == ERR_MP3_NONE &&
       player_mp3_frame_info_valid(&next_info) != 0U &&
       player_mp3_frame_infos_match(frame_info, &next_info) != 0U)
    {
      return 1U;
    }
    search_offset++;
  }

  return 0U;
}

static u8 player_mp3_candidate_matches(Player_MP3_Context_t *mp3,
                                       const u8 *frame,
                                       u32 available,
                                       MP3FrameInfo *frame_info)
{
  memset(frame_info, 0, sizeof(*frame_info));
  if(player_mp3_header_valid(frame, available) == 0U ||
     MP3GetNextFrameInfo(mp3->decoder, frame_info,
                         (unsigned char *)frame) != ERR_MP3_NONE ||
     player_mp3_frame_info_valid(frame_info) == 0U)
  {
    return 0U;
  }

  if(mp3->format_locked != 0U)
  {
    return (mp3->expected_output_samps == (u32)frame_info->outputSamps &&
            mp3->expected_sample_rate == (u32)frame_info->samprate &&
            mp3->expected_channels == (u8)frame_info->nChans) ? 1U : 0U;
  }

  return player_mp3_find_matching_header(mp3, frame, available, frame_info);
}

static int player_mp3_decode_next(Player_MP3_Context_t *mp3,
                                  u32 *pcm_bytes,
                                  MP3FrameInfo *decoded_info)
{
  MP3FrameInfo frame_info;
  MP3FrameInfo candidate_info;
  unsigned char *read_ptr;
  u8 *candidate_ptr;
  u32 candidate_offset;
  u32 candidate_bytes;
  u32 pointer_consumed;
  u32 count_consumed;
  int sync_offset;
  int decode_result;
  int refill_result;
  u32 attempts = 0U;

  *pcm_bytes = 0U;
  while(attempts++ < PLAYER_MP3_MAX_RECOVERY_ATTEMPTS)
  {
    refill_result = player_mp3_refill(mp3);
    if(refill_result != PLAYER_CODEC_READ_OK)
    {
      return refill_result;
    }

    read_ptr = (unsigned char *)(mp3->input_buffer + mp3->read_offset);
    sync_offset = MP3FindSyncWord(read_ptr, mp3->bytes_left);
    if(sync_offset < 0)
    {
      s_player_mp3_stats.sync_miss_count++;
      if(mp3->eof != 0U)
      {
        player_mp3_advance(mp3, (u32)mp3->bytes_left);
        return PLAYER_CODEC_READ_EOF;
      }
      if(mp3->bytes_left > 1)
      {
        player_mp3_advance(mp3, (u32)mp3->bytes_left - 1U);
      }
      continue;
    }

    player_mp3_advance(mp3, (u32)sync_offset);
    candidate_offset = mp3->read_offset;
    candidate_bytes = (u32)mp3->bytes_left;
    candidate_ptr = mp3->input_buffer + candidate_offset;
    if(player_mp3_candidate_matches(mp3, candidate_ptr, candidate_bytes,
                                    &candidate_info) == 0U)
    {
      if(player_mp3_frame_info_valid(&candidate_info) != 0U)
      {
        player_mp3_note_bad_format(&candidate_info);
      }
      player_mp3_recover(mp3);
      continue;
    }

    read_ptr = (unsigned char *)candidate_ptr;
    decode_result = MP3Decode(mp3->decoder,
                              &read_ptr,
                              &mp3->bytes_left,
                              (short *)mp3->pcm_buffer,
                              0);
    pointer_consumed = (u32)(read_ptr - candidate_ptr);
    count_consumed = candidate_bytes - (u32)mp3->bytes_left;
    memset(&frame_info, 0, sizeof(frame_info));
    MP3GetLastFrameInfo(mp3->decoder, &frame_info);
    if(decode_result != ERR_MP3_NONE)
    {
      s_player_mp3_stats.decode_error_count++;
      s_player_mp3_stats.last_decode_error = (s32)decode_result;
      if(decode_result == ERR_MP3_MAINDATA_UNDERFLOW)
      {
        s_player_mp3_stats.main_data_underflow_count++;
      }

      if(decode_result == ERR_MP3_INVALID_FRAMEHEADER)
      {
        mp3->read_offset = candidate_offset;
        mp3->bytes_left = (int)candidate_bytes;
        player_mp3_recover(mp3);
        continue;
      }

      if(decode_result == ERR_MP3_INDATA_UNDERFLOW)
      {
        mp3->read_offset = candidate_offset;
        mp3->bytes_left = (int)candidate_bytes;
        if(mp3->eof != 0U)
        {
          return PLAYER_CODEC_READ_EOF;
        }
        if(candidate_bytes >= PLAYER_MP3_INPUT_SIZE)
        {
          return PLAYER_CODEC_FORMAT_ERROR;
        }
        continue;
      }

      if(player_mp3_error_is_concealed(decode_result) == 0U)
      {
        if(decode_result == ERR_MP3_OUT_OF_MEMORY ||
           decode_result == ERR_MP3_NULL_POINTER)
        {
          return PLAYER_CODEC_ERROR;
        }
        mp3->read_offset = candidate_offset;
        mp3->bytes_left = (int)candidate_bytes;
        player_mp3_recover(mp3);
        continue;
      }
    }

    if(pointer_consumed > candidate_bytes || count_consumed > candidate_bytes ||
       pointer_consumed != count_consumed)
    {
      mp3->read_offset = candidate_offset;
      mp3->bytes_left = (int)candidate_bytes;
      player_mp3_recover(mp3);
      continue;
    }
    mp3->read_offset = candidate_offset + pointer_consumed;

    if(player_mp3_frame_infos_match(&candidate_info, &frame_info) == 0U ||
       (decode_result == ERR_MP3_NONE &&
        player_mp3_output_matches(mp3, &frame_info) == 0U))
    {
      player_mp3_note_bad_format(&frame_info);
      continue;
    }

    if(mp3->format_locked == 0U)
    {
      /* Do not lock the output format from a concealed or underflow frame. */
      continue;
    }

    if(decode_result != ERR_MP3_NONE)
    {
      s_player_mp3_stats.concealed_frame_count++;
    }
    *pcm_bytes = (u32)frame_info.outputSamps * (u32)sizeof(s16);
    if(decoded_info != NULL)
    {
      *decoded_info = frame_info;
    }
    return PLAYER_CODEC_READ_OK;
  }

  return PLAYER_CODEC_FORMAT_ERROR;
}

static int player_mp3_read_pcm(void *context, const u8 **pcm, u32 *pcm_bytes)
{
  Player_MP3_Context_t *mp3 = (Player_MP3_Context_t *)context;
  int result;

  if(mp3 == NULL || pcm == NULL || pcm_bytes == NULL)
  {
    return PLAYER_CODEC_ERROR;
  }

  *pcm = (const u8 *)mp3->pcm_buffer;
  if(mp3->pcm_ready != 0U)
  {
    mp3->pcm_ready = 0U;
    *pcm_bytes = mp3->ready_bytes;
    player_mp3_count_pcm(mp3, *pcm_bytes);
    return PLAYER_CODEC_READ_OK;
  }

  result = player_mp3_decode_next(mp3, pcm_bytes, NULL);
  if(result == PLAYER_CODEC_READ_OK)
  {
    player_mp3_count_pcm(mp3, *pcm_bytes);
  }
  return result;
}

static int player_mp3_seek(void *context, u16 position)
{
  Player_MP3_Context_t *mp3 = (Player_MP3_Context_t *)context;
  u32 target;

  if(mp3 == NULL || mp3->file == NULL)
  {
    return PLAYER_CODEC_ERROR;
  }

  if(position > 1000U)
  {
    position = 1000U;
  }

  target = (mp3->audio_size / 1000U) * (u32)position;
  target += ((mp3->audio_size % 1000U) * (u32)position) / 1000U;
  if(target > mp3->audio_size)
  {
    target = mp3->audio_size;
  }

  if(f_lseek(mp3->file, (FSIZE_t)(mp3->audio_start_offset + target)) != FR_OK)
  {
    return PLAYER_CODEC_ERROR;
  }

  if(mp3->decoder != NULL)
  {
    MP3FreeDecoder(mp3->decoder);
  }
  mp3->decoder = MP3InitDecoder();
  if(mp3->decoder == NULL)
  {
    return PLAYER_CODEC_MEMORY_ERROR;
  }

  mp3->bytes_left = 0;
  mp3->read_offset = 0U;
  mp3->ready_bytes = 0U;
  mp3->decoded_pcm_bytes = 0U;
  mp3->position_base_seconds =
      (mp3->duration_seconds / 1000U) * (u32)position +
      ((mp3->duration_seconds % 1000U) * (u32)position) / 1000U;
  mp3->eof = 0U;
  mp3->pcm_ready = 0U;

  return PLAYER_CODEC_READ_OK;
}

static u32 player_mp3_tell_seconds(void *context)
{
  Player_MP3_Context_t *mp3 = (Player_MP3_Context_t *)context;
  u32 seconds;

  if(mp3 == NULL || mp3->pcm_byte_rate == 0U)
  {
    return 0U;
  }

  seconds = mp3->position_base_seconds +
            (mp3->decoded_pcm_bytes / mp3->pcm_byte_rate);
  if(mp3->duration_seconds != 0U && seconds > mp3->duration_seconds)
  {
    seconds = mp3->duration_seconds;
  }

  return seconds;
}

static void player_mp3_close(void *context)
{
  Player_MP3_Context_t *mp3 = (Player_MP3_Context_t *)context;

  if(mp3 == NULL)
  {
    return;
  }

  if(mp3->decoder != NULL)
  {
    MP3FreeDecoder(mp3->decoder);
  }
  if(mp3->pcm_buffer != NULL)
  {
    sys_free(PLAYER_MP3_MEM_POOL, mp3->pcm_buffer);
  }
  if(mp3->input_buffer != NULL)
  {
    sys_free(PLAYER_MP3_MEM_POOL, mp3->input_buffer);
  }
  sys_free(PLAYER_MP3_MEM_POOL, mp3);
}

int Player_MP3_Open(FIL *file, Player_Codec_Handle_t *codec, Player_Info_t *info)
{
  Player_MP3_Context_t *mp3;
  MP3FrameInfo frame_info;
  u32 audio_start_offset = 0U;
  u32 pcm_bytes;
  int result;

  if(file == NULL || codec == NULL || info == NULL)
  {
    return PLAYER_CODEC_ERROR;
  }

  memset(codec, 0, sizeof(*codec));
  memset(info, 0, sizeof(*info));
  Player_MP3_ResetStats();
  result = player_mp3_skip_id3v2(file, &audio_start_offset);
  if(result != PLAYER_CODEC_READ_OK)
  {
    return result;
  }

  mp3 = (Player_MP3_Context_t *)sys_malloc(PLAYER_MP3_MEM_POOL,
                                            (u32)sizeof(Player_MP3_Context_t));
  if(mp3 == NULL)
  {
    return PLAYER_CODEC_MEMORY_ERROR;
  }
  memset(mp3, 0, sizeof(*mp3));
  mp3->file = file;
  mp3->audio_start_offset = audio_start_offset;
  if(f_size(file) > (FSIZE_t)audio_start_offset)
  {
    mp3->audio_size = (u32)(f_size(file) - (FSIZE_t)audio_start_offset);
  }

  mp3->input_buffer = (u8 *)sys_malloc(PLAYER_MP3_MEM_POOL, PLAYER_MP3_INPUT_SIZE);
  mp3->pcm_buffer = (s16 *)sys_malloc(PLAYER_MP3_MEM_POOL,
                                      PLAYER_MP3_MAX_PCM_SAMPLES * (u32)sizeof(s16));
  mp3->decoder = MP3InitDecoder();
  if(mp3->input_buffer == NULL || mp3->pcm_buffer == NULL || mp3->decoder == NULL)
  {
    player_mp3_close(mp3);
    return PLAYER_CODEC_MEMORY_ERROR;
  }

  memset(&frame_info, 0, sizeof(frame_info));
  result = player_mp3_decode_next(mp3, &pcm_bytes, &frame_info);
  if(result != PLAYER_CODEC_READ_OK || pcm_bytes == 0U)
  {
    player_mp3_close(mp3);
    return (result == PLAYER_CODEC_READ_EOF) ? PLAYER_CODEC_FORMAT_ERROR : result;
  }

  mp3->duration_seconds =
      player_mp3_estimate_duration(mp3->audio_size, (u32)frame_info.bitrate);
  mp3->pcm_byte_rate = (u32)frame_info.samprate *
                       (u32)frame_info.nChans *
                       (u32)sizeof(s16);
  mp3->ready_bytes = pcm_bytes;
  mp3->pcm_ready = 1U;
  codec->context = mp3;
  codec->read_pcm = player_mp3_read_pcm;
  codec->seek = player_mp3_seek;
  codec->tell_seconds = player_mp3_tell_seconds;
  codec->close = player_mp3_close;
  codec->package_size = pcm_bytes;

  info->codec = PLAYER_CODEC_MP3;
  info->duration_seconds = mp3->duration_seconds;
  info->sample_rate = (u32)frame_info.samprate;
  info->bitrate = (u32)frame_info.bitrate;
  info->channels = (u8)frame_info.nChans;
  info->bits_per_sample = 16U;

  return PLAYER_CODEC_READ_OK;
}

void Player_MP3_GetStats(Player_MP3_Stats_t *stats)
{
  if(stats == NULL)
  {
    return;
  }

  __DMB();
  *stats = s_player_mp3_stats;
  __DMB();
}

void Player_MP3_ResetStats(void)
{
  memset(&s_player_mp3_stats, 0, sizeof(s_player_mp3_stats));
  __DMB();
}
