#include "Player_WAV.h"

#include "Malloc.h"

#include <string.h>

#define PLAYER_WAV_BUFFER_SIZE 4096U

typedef struct Player_WAV_Context
{
  FIL *file;
  u8 *pcm_buffer;
  u32 bytes_remaining;
} Player_WAV_Context_t;

static u16 player_wav_read_le16(const u8 *data)
{
  return (u16)data[0] | ((u16)data[1] << 8);
}

static u32 player_wav_read_le32(const u8 *data)
{
  return (u32)data[0] |
         ((u32)data[1] << 8) |
         ((u32)data[2] << 16) |
         ((u32)data[3] << 24);
}

static int player_wav_skip(FIL *file, u32 size)
{
  FSIZE_t target = f_tell(file) + (FSIZE_t)size;

  if(target < f_tell(file) || target > f_size(file))
  {
    return PLAYER_CODEC_FORMAT_ERROR;
  }

  return (f_lseek(file, target) == FR_OK) ? PLAYER_CODEC_READ_OK : PLAYER_CODEC_ERROR;
}

static int player_wav_read_pcm(void *context, const u8 **pcm, u32 *pcm_bytes)
{
  Player_WAV_Context_t *wav = (Player_WAV_Context_t *)context;
  UINT bytes_read = 0U;
  u32 request_size;

  if(wav == NULL || pcm == NULL || pcm_bytes == NULL)
  {
    return PLAYER_CODEC_ERROR;
  }

  *pcm = wav->pcm_buffer;
  *pcm_bytes = 0U;
  if(wav->bytes_remaining == 0U)
  {
    return PLAYER_CODEC_READ_EOF;
  }

  request_size = (wav->bytes_remaining < PLAYER_WAV_BUFFER_SIZE) ?
                 wav->bytes_remaining : PLAYER_WAV_BUFFER_SIZE;
  if(f_read(wav->file, wav->pcm_buffer, request_size, &bytes_read) != FR_OK || bytes_read == 0U)
  {
    return PLAYER_CODEC_ERROR;
  }

  if((u32)bytes_read > wav->bytes_remaining)
  {
    return PLAYER_CODEC_ERROR;
  }

  wav->bytes_remaining -= (u32)bytes_read;
  *pcm_bytes = (u32)bytes_read;

  if((u32)bytes_read < request_size)
  {
    wav->bytes_remaining = 0U;
  }

  return (wav->bytes_remaining == 0U) ? PLAYER_CODEC_READ_EOF : PLAYER_CODEC_READ_OK;
}

static void player_wav_close(void *context)
{
  Player_WAV_Context_t *wav = (Player_WAV_Context_t *)context;

  if(wav == NULL)
  {
    return;
  }

  if(wav->pcm_buffer != NULL)
  {
    sys_free(SRAMEX, wav->pcm_buffer);
  }
  sys_free(SRAMEX, wav);
}

int Player_WAV_Open(FIL *file, Player_Codec_Handle_t *codec, Player_Info_t *info)
{
  Player_WAV_Context_t *wav;
  u8 header[16];
  UINT bytes_read;
  u32 chunk_size;
  u32 byte_rate = 0U;
  u32 data_size = 0U;
  u32 sample_rate = 0U;
  u16 channels = 0U;
  u16 bits_per_sample = 0U;
  u8 fmt_found = 0U;

  if(file == NULL || codec == NULL || info == NULL)
  {
    return PLAYER_CODEC_ERROR;
  }

  memset(codec, 0, sizeof(*codec));
  memset(info, 0, sizeof(*info));
  if(f_lseek(file, 0U) != FR_OK ||
     f_read(file, header, 12U, &bytes_read) != FR_OK || bytes_read != 12U)
  {
    return PLAYER_CODEC_ERROR;
  }

  if(memcmp(header, "RIFF", 4U) != 0 || memcmp(&header[8], "WAVE", 4U) != 0)
  {
    return PLAYER_CODEC_FORMAT_ERROR;
  }

  while(f_tell(file) + 8U <= f_size(file))
  {
    if(f_read(file, header, 8U, &bytes_read) != FR_OK || bytes_read != 8U)
    {
      return PLAYER_CODEC_ERROR;
    }

    chunk_size = player_wav_read_le32(&header[4]);
    if(memcmp(header, "fmt ", 4U) == 0)
    {
      if(chunk_size < 16U ||
         f_read(file, header, 16U, &bytes_read) != FR_OK || bytes_read != 16U)
      {
        return PLAYER_CODEC_FORMAT_ERROR;
      }

      if(player_wav_read_le16(&header[0]) != 1U)
      {
        return PLAYER_CODEC_UNSUPPORTED;
      }

      channels = player_wav_read_le16(&header[2]);
      sample_rate = player_wav_read_le32(&header[4]);
      byte_rate = player_wav_read_le32(&header[8]);
      bits_per_sample = player_wav_read_le16(&header[14]);
      if((channels != 1U && channels != 2U) ||
         sample_rate == 0U || byte_rate == 0U ||
         (bits_per_sample != 8U && bits_per_sample != 16U))
      {
        return PLAYER_CODEC_UNSUPPORTED;
      }

      fmt_found = 1U;
      if(player_wav_skip(file, (chunk_size - 16U) + (chunk_size & 1U)) != PLAYER_CODEC_READ_OK)
      {
        return PLAYER_CODEC_FORMAT_ERROR;
      }
    }
    else if(memcmp(header, "data", 4U) == 0 && fmt_found != 0U)
    {
      data_size = chunk_size;
      break;
    }
    else if(player_wav_skip(file, chunk_size + (chunk_size & 1U)) != PLAYER_CODEC_READ_OK)
    {
      return PLAYER_CODEC_FORMAT_ERROR;
    }
  }

  if(fmt_found == 0U || data_size == 0U)
  {
    return PLAYER_CODEC_FORMAT_ERROR;
  }

  wav = (Player_WAV_Context_t *)sys_malloc(SRAMEX, (u32)sizeof(Player_WAV_Context_t));
  if(wav == NULL)
  {
    return PLAYER_CODEC_MEMORY_ERROR;
  }
  memset(wav, 0, sizeof(*wav));

  wav->pcm_buffer = (u8 *)sys_malloc(SRAMEX, PLAYER_WAV_BUFFER_SIZE);
  if(wav->pcm_buffer == NULL)
  {
    player_wav_close(wav);
    return PLAYER_CODEC_MEMORY_ERROR;
  }

  wav->file = file;
  wav->bytes_remaining = data_size;
  codec->context = wav;
  codec->read_pcm = player_wav_read_pcm;
  codec->close = player_wav_close;
  codec->package_size = PLAYER_WAV_BUFFER_SIZE;

  info->codec = PLAYER_CODEC_WAV;
  info->duration_seconds = data_size / byte_rate;
  info->sample_rate = sample_rate;
  info->bitrate = byte_rate * 8U;
  info->channels = (u8)channels;
  info->bits_per_sample = (u8)bits_per_sample;

  return PLAYER_CODEC_READ_OK;
}
