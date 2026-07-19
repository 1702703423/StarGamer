#include "Player.h"
#include "Player_Codec.h"
#include "Player_MP3.h"
#include "Player_WAV.h"

#include "FreeRTOS.h"
#include "Malloc.h"
#include "System_Status.h"
#include "i2s.h"
#include "shell.h"
#include "shell_support.h"
#include "task.h"

#include <string.h>

extern Shell shell;

typedef StaticTask_t osStaticThreadDef_t;

#define EVENT_HALF_DMA       (1U << 0)
#define EVENT_OVER_DMA       (1U << 1)
#define EVENT_STOP_PLAYER     (1U << 2)
#define EVENT_PAUSE_PLAYER    (1U << 4)
#define EVENT_RESUME_PLAYER   (1U << 5)
#define EVENT_DMA_EMPTY       (EVENT_HALF_DMA | EVENT_OVER_DMA)
#define EVENT_PLAYER_WAIT     (EVENT_DMA_EMPTY | EVENT_STOP_PLAYER | EVENT_PAUSE_PLAYER)
#define PLAYER_PATH_SIZE      256U
#define PLAYER_TASK_STACK_SIZE (3072U * sizeof(u32))
#define PLAYER_CLEANUP_DELAY_TICKS 2U

static osThreadId_t s_player_task_handle;
static FIL s_player_file;
static u8 s_player_file_opened;
static Player_Codec_Handle_t s_player_codec;
static volatile Player_State_t s_player_state = PLAYER_STATE_IDLE;
static volatile u32 s_player_task_exit_tick;
static volatile u32 s_player_task_stack_space;
static volatile u32 s_player_codec_read_count;
static volatile u32 s_player_codec_read_max_ticks;
static volatile u32 s_player_write_max_ticks;
static volatile u32 s_player_dma_both_event_count;
static Player_Info_t s_player_info;
static TCHAR s_player_path[PLAYER_PATH_SIZE];

static osStaticThreadDef_t s_player_task_control_block;
static osThreadAttr_t s_player_task_attr =
{
  .name = "WaveDataTransmitTask",
  .cb_mem = &s_player_task_control_block,
  .cb_size = sizeof(s_player_task_control_block),
  .stack_mem = NULL,
  .stack_size = PLAYER_TASK_STACK_SIZE,
  .priority = (osPriority_t)osPriorityHigh2,
};

static void WaveDataTransmitTask(void *argument);
static void audio_feeder(void);

static void player_update_max(volatile u32 *target, u32 value)
{
  if(value > *target)
  {
    *target = value;
  }
}

static void player_reset_audio_stats(void)
{
  s_player_codec_read_count = 0U;
  s_player_codec_read_max_ticks = 0U;
  s_player_write_max_ticks = 0U;
  s_player_dma_both_event_count = 0U;
  audio_card_reset_stats();
  Player_MP3_ResetStats();
}

static u8 player_copy_path(TCHAR *dst, u32 dst_size, const TCHAR *src)
{
  u32 i;

  if(dst == NULL || dst_size == 0U || src == NULL)
  {
    return 0U;
  }

  for(i = 0U; i < dst_size - 1U; i++)
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

static TCHAR player_ascii_lower(TCHAR value)
{
  if(value >= (TCHAR)'A' && value <= (TCHAR)'Z')
  {
    return (TCHAR)(value + ((TCHAR)'a' - (TCHAR)'A'));
  }
  return value;
}

static u8 player_has_extension(const TCHAR *path, const char *extension)
{
  u32 path_length = 0U;
  u32 extension_length = 0U;
  u32 i;

  if(path == NULL || extension == NULL)
  {
    return 0U;
  }

  while(path[path_length] != '\0' && path_length < PLAYER_PATH_SIZE)
  {
    path_length++;
  }
  while(extension[extension_length] != '\0')
  {
    extension_length++;
  }

  if(path_length >= PLAYER_PATH_SIZE || path_length < extension_length)
  {
    return 0U;
  }

  for(i = 0U; i < extension_length; i++)
  {
    if(player_ascii_lower(path[path_length - extension_length + i]) !=
       player_ascii_lower((TCHAR)extension[i]))
    {
      return 0U;
    }
  }

  return 1U;
}

static void player_free_stack(void)
{
  if(s_player_task_attr.stack_mem != NULL)
  {
    sys_thread_stack_free(s_player_task_attr.stack_mem);
    s_player_task_attr.stack_mem = NULL;
  }
}

static void player_close_codec(void)
{
  if(s_player_codec.close != NULL && s_player_codec.context != NULL)
  {
    s_player_codec.close(s_player_codec.context);
  }
  memset(&s_player_codec, 0, sizeof(s_player_codec));
}

static void player_close_file(void)
{
  if(s_player_file_opened != 0U)
  {
    f_close(&s_player_file);
    s_player_file_opened = 0U;
  }
}

static int player_open_codec(const TCHAR *path, Player_Info_t *info)
{
  if(player_has_extension(path, ".wav") != 0U)
  {
    return Player_WAV_Open(&s_player_file, &s_player_codec, info);
  }
  if(player_has_extension(path, ".mp3") != 0U)
  {
    return Player_MP3_Open(&s_player_file, &s_player_codec, info);
  }
  return PLAYER_CODEC_UNSUPPORTED;
}

static int player_fill_dma_buffer(u8 dma_index)
{
  const u8 *pcm = NULL;
  u32 pcm_bytes = 0U;
  u32 start_tick;
  u32 elapsed_ticks;
  int codec_result;
  int write_result;

  if(s_player_codec.read_pcm == NULL)
  {
    return -1;
  }

  start_tick = osKernelGetTickCount();
  codec_result = s_player_codec.read_pcm(s_player_codec.context, &pcm, &pcm_bytes);
  elapsed_ticks = osKernelGetTickCount() - start_tick;
  s_player_codec_read_count++;
  player_update_max(&s_player_codec_read_max_ticks, elapsed_ticks);
  if(codec_result < PLAYER_CODEC_READ_OK)
  {
    shellPrintEndLine(&shell, "TASK_ERROR:CODEC_READ:%d\r\n", codec_result);
    return -1;
  }

  start_tick = osKernelGetTickCount();
  if(pcm_bytes == 0U)
  {
    write_result = audio_card_write_silence(dma_index);
  }
  else
  {
    write_result = audio_card_write_to_buffer(dma_index, (void *)pcm, (int)pcm_bytes);
  }
  elapsed_ticks = osKernelGetTickCount() - start_tick;
  player_update_max(&s_player_write_max_ticks, elapsed_ticks);
  if(write_result != 0)
  {
    return -1;
  }

  return (codec_result == PLAYER_CODEC_READ_EOF) ? 1 : 0;
}

static int player_service_dma_buffer(u8 dma_index, u8 *drain_events)
{
  int fill_result;

  if(*drain_events > 0U)
  {
    if(audio_card_write_silence(dma_index) != 0)
    {
      return -1;
    }

    (*drain_events)--;
    return (*drain_events == 0U) ? 1 : 0;
  }

  fill_result = player_fill_dma_buffer(dma_index);
  if(fill_result < 0)
  {
    return -1;
  }
  if(fill_result > 0)
  {
    *drain_events = 2U;
  }

  return 0;
}

static void player_exit(Player_State_t final_state)
{
  osThreadId_t tid = s_player_task_handle;

  s_player_state = final_state;
  audio_card_register_tx(NULL);
  audio_card_close();
  player_close_codec();
  player_close_file();

  if(tid != NULL)
  {
    SysUnregisterThread(tid);
  }

  s_player_task_stack_space = osThreadGetStackSpace(tid);
  __DMB();
  s_player_task_exit_tick = (u32)xTaskGetTickCount();
  __DMB();
  osThreadExit();
}

static void player_pause_wait(void)
{
  u32 flags;

  if(audio_card_pause() != 0)
  {
    shellPrintEndLine(&shell, "TASK_ERROR:AUDIO_PAUSE\r\n");
    player_exit(PLAYER_STATE_ERROR);
  }

  s_player_state = PLAYER_STATE_PAUSED;
  while(1)
  {
    flags = osThreadFlagsWait(EVENT_RESUME_PLAYER | EVENT_STOP_PLAYER,
                              osFlagsWaitAny,
                              osWaitForever);
    if((flags & osFlagsError) != 0U)
    {
      shellPrintEndLine(&shell, "PauseWaitError\r\n");
      player_exit(PLAYER_STATE_ERROR);
    }
    if((flags & EVENT_STOP_PLAYER) != 0U)
    {
      player_exit(PLAYER_STATE_IDLE);
    }
    if((flags & EVENT_RESUME_PLAYER) != 0U)
    {
      if(audio_card_resume() != 0)
      {
        shellPrintEndLine(&shell, "TASK_ERROR:AUDIO_RESUME\r\n");
        player_exit(PLAYER_STATE_ERROR);
      }
      s_player_state = PLAYER_STATE_PLAYING;
      return;
    }
  }
}

void WavePlayTaskStop(osThreadId_t tid)
{
  if(tid == NULL || tid != s_player_task_handle)
  {
    return;
  }

  s_player_task_handle = NULL;
  s_player_task_exit_tick = 0U;
  s_player_task_stack_space = 0U;
  s_player_state = PLAYER_STATE_IDLE;
  audio_card_register_tx(NULL);
  audio_card_close();
  SysUnregisterThread(tid);
  vTaskDelete((TaskHandle_t)tid);
  player_close_codec();
  player_close_file();
  player_free_stack();
}

int PlayerStart(const TCHAR *audio_path)
{
  Player_Info_t info;
  FRESULT file_result;
  int codec_result;

  PlayerCleanup();
  if(audio_path == NULL)
  {
    shellPrintEndLine(&shell, "PlayerStart:NULL_PATH\r\n");
    return OPERATE_FAILED;
  }
  if(s_player_task_handle != NULL)
  {
    shellPrintEndLine(&shell, "PlayerStart:BUSY\r\n");
    return OPERATE_FAILED;
  }
  if(player_copy_path(s_player_path, PLAYER_PATH_SIZE, audio_path) == 0U)
  {
    shellPrintEndLine(&shell, "PlayerStart:PATH_TOO_LONG\r\n");
    return OPERATE_FAILED;
  }

  player_close_codec();
  player_close_file();
  memset(&info, 0, sizeof(info));
  memset(&s_player_info, 0, sizeof(s_player_info));
  s_player_task_stack_space = 0U;
  s_player_task_exit_tick = 0U;
  s_player_state = PLAYER_STATE_STARTING;

  file_result = f_open(&s_player_file, audio_path, FA_READ);
  if(file_result != FR_OK)
  {
    shellPrintEndLine(&shell, "PlayerStart:f_open:%d\r\n", file_result);
    s_player_state = PLAYER_STATE_ERROR;
    return OPERATE_FAILED;
  }
  s_player_file_opened = 1U;

  codec_result = player_open_codec(audio_path, &info);
  if(codec_result != PLAYER_CODEC_READ_OK)
  {
    shellPrintEndLine(&shell, "PlayerStart:codec:%d\r\n", codec_result);
    player_close_codec();
    player_close_file();
    s_player_state = PLAYER_STATE_ERROR;
    return OPERATE_FAILED;
  }

  player_reset_audio_stats();
  audio_card_init(audio_card_get_volume());
  if(audio_card_open_prepare((int)info.sample_rate,
                             (int)info.bits_per_sample,
                             (int)info.channels,
                             (int)s_player_codec.package_size) != 0)
  {
    shellPrintEndLine(&shell, "PlayerStart:AUDIO_UNSUPPORTED\r\n");
    player_close_codec();
    player_close_file();
    s_player_state = PLAYER_STATE_ERROR;
    return OPERATE_FAILED;
  }

  audio_card_register_tx(audio_feeder);
  if(s_player_task_attr.stack_mem == NULL)
  {
    s_player_task_attr.stack_mem = sys_thread_stack_alloc((u32)s_player_task_attr.stack_size);
  }
  if(s_player_task_attr.stack_mem == NULL)
  {
    audio_card_register_tx(NULL);
    audio_card_close();
    player_close_codec();
    player_close_file();
    s_player_state = PLAYER_STATE_ERROR;
    return OPERATE_FAILED;
  }

  s_player_info = info;
  s_player_task_handle = SysCreateThread(WaveDataTransmitTask,
                                         &s_player_file,
                                         &s_player_task_attr,
                                         0U);
  if(s_player_task_handle == NULL)
  {
    audio_card_register_tx(NULL);
    audio_card_close();
    player_close_codec();
    player_close_file();
    player_free_stack();
    memset(&s_player_info, 0, sizeof(s_player_info));
    s_player_state = PLAYER_STATE_ERROR;
    return OPERATE_FAILED;
  }

  shellPrintEndLine(&shell, "Player codec=%d sample=%lu channels=%u bits=%u bitrate=%lu\r\n",
                    (int)info.codec,
                    (unsigned long)info.sample_rate,
                    (unsigned int)info.channels,
                    (unsigned int)info.bits_per_sample,
                    (unsigned long)info.bitrate);
  return SYSTEM_STATUS_OK;
}

void PlayerCleanup(void)
{
  u32 now;

  if(s_player_task_handle == NULL || s_player_task_exit_tick == 0U)
  {
    return;
  }

  now = (u32)xTaskGetTickCount();
  if((now - s_player_task_exit_tick) < PLAYER_CLEANUP_DELAY_TICKS)
  {
    return;
  }

  player_free_stack();
  s_player_task_handle = NULL;
  s_player_task_exit_tick = 0U;
}

int PlayerPause(void)
{
  u32 flags;

  PlayerCleanup();
  if(s_player_task_handle == NULL || s_player_task_exit_tick != 0U)
  {
    return OPERATE_FAILED;
  }
  if(s_player_state == PLAYER_STATE_PAUSED)
  {
    return SYSTEM_STATUS_OK;
  }

  flags = osThreadFlagsSet(s_player_task_handle, EVENT_PAUSE_PLAYER);
  return ((flags & osFlagsError) != 0U) ? OPERATE_FAILED : SYSTEM_STATUS_OK;
}

int PlayerResume(void)
{
  u32 flags;

  PlayerCleanup();
  if(s_player_task_handle == NULL || s_player_task_exit_tick != 0U)
  {
    return OPERATE_FAILED;
  }
  if(s_player_state != PLAYER_STATE_PAUSED)
  {
    return SYSTEM_STATUS_OK;
  }

  flags = osThreadFlagsSet(s_player_task_handle, EVENT_RESUME_PLAYER);
  return ((flags & osFlagsError) != 0U) ? OPERATE_FAILED : SYSTEM_STATUS_OK;
}

int PlayerTogglePause(void)
{
  return (s_player_state == PLAYER_STATE_PAUSED) ? PlayerResume() : PlayerPause();
}

int PlayerStop(void)
{
  u32 flags;

  PlayerCleanup();
  if(s_player_task_handle == NULL || s_player_task_exit_tick != 0U)
  {
    s_player_state = PLAYER_STATE_IDLE;
    return SYSTEM_STATUS_OK;
  }

  s_player_state = PLAYER_STATE_STOPPING;
  flags = osThreadFlagsSet(s_player_task_handle, EVENT_STOP_PLAYER);
  return ((flags & osFlagsError) != 0U) ? OPERATE_FAILED : SYSTEM_STATUS_OK;
}

u8 PlayerIsBusy(void)
{
  PlayerCleanup();
  return (s_player_task_handle != NULL) ? 1U : 0U;
}

Player_State_t PlayerGetState(void)
{
  return s_player_state;
}

int PlayerGetInfo(Player_Info_t *info)
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

static void WaveDataTransmitTask(void *argument)
{
  u32 flags;
  int fill_result;
  u8 drain_events = 0U;

  (void)argument;
  s_player_task_handle = osThreadGetId();
  __DMB();

  fill_result = player_fill_dma_buffer(0U);
  if(fill_result < 0)
  {
    player_exit(PLAYER_STATE_ERROR);
  }

  if(fill_result == 0)
  {
    fill_result = player_fill_dma_buffer(1U);
    if(fill_result < 0)
    {
      player_exit(PLAYER_STATE_ERROR);
    }
  }
  else if(audio_card_write_silence(1U) != 0)
  {
    player_exit(PLAYER_STATE_ERROR);
  }

  if(fill_result > 0)
  {
    drain_events = 2U;
  }

  if(audio_card_start() != 0)
  {
    shellPrintEndLine(&shell, "TASK_ERROR:AUDIO_START\r\n");
    player_exit(PLAYER_STATE_ERROR);
  }
  s_player_state = PLAYER_STATE_PLAYING;

  while(1)
  {
    flags = osThreadFlagsWait(EVENT_PLAYER_WAIT, osFlagsWaitAny, 20000U);
    if((flags & osFlagsError) != 0U)
    {
      shellPrintEndLine(&shell, "WaitEventError\r\n");
      player_exit(PLAYER_STATE_ERROR);
    }
    if((flags & EVENT_STOP_PLAYER) != 0U)
    {
      player_exit(PLAYER_STATE_IDLE);
    }

    if((flags & EVENT_DMA_EMPTY) == EVENT_DMA_EMPTY)
    {
      s_player_dma_both_event_count++;
      fill_result = player_service_dma_buffer(audio_card_get_idle_buffer_index(), &drain_events);
    }
    else if((flags & EVENT_HALF_DMA) != 0U)
    {
      fill_result = player_service_dma_buffer(0U, &drain_events);
    }
    else if((flags & EVENT_OVER_DMA) != 0U)
    {
      fill_result = player_service_dma_buffer(1U, &drain_events);
    }
    else
    {
      fill_result = 0;
    }

    if(fill_result != 0)
    {
      if(fill_result > 0)
      {
        shellPrintEndLine(&shell, "PlayOver\r\n");
      }
      player_exit((fill_result > 0) ? PLAYER_STATE_IDLE : PLAYER_STATE_ERROR);
    }

    if((flags & EVENT_PAUSE_PLAYER) != 0U)
    {
      player_pause_wait();
    }
  }
}

static void audio_feeder(void)
{
  u8 dma_index = audio_card_get_idle_buffer_index();

  if(s_player_task_handle == NULL || s_player_task_exit_tick != 0U)
  {
    return;
  }

  if(dma_index == 0U)
  {
    osThreadFlagsSet(s_player_task_handle, EVENT_HALF_DMA);
  }
  else
  {
    osThreadFlagsSet(s_player_task_handle, EVENT_OVER_DMA);
  }
}

int player_audio_stats_cmd(int argc, char *argv[])
{
  audio_card_stats_t stats;
  Player_MP3_Stats_t mp3_stats;
  u32 stack_space;

  if(argc > 1 && argv[1] != NULL && argv[1][0] == 'r')
  {
    player_reset_audio_stats();
    shellPrintEndLine(&shell, "Audio stats reset\r\n");
    return 0;
  }

  audio_card_get_stats(&stats);
  Player_MP3_GetStats(&mp3_stats);
  stack_space = s_player_task_stack_space;
  if(s_player_task_handle != NULL && s_player_task_exit_tick == 0U)
  {
    stack_space = osThreadGetStackSpace(s_player_task_handle);
  }
  shellPrintEndLine(&shell, "Audio stats:\r\n");
  shellPrintEndLine(&shell, "  freq=%lu pkg=%lu pcm=%lu\r\n",
                    (unsigned long)audio_card_get_freq(),
                    (unsigned long)audio_card_get_pkg_size(),
                    (unsigned long)audio_card_get_pcm_size());
  shellPrintEndLine(&shell, "  dma half=%lu full=%lu both=%lu late=%lu\r\n",
                    (unsigned long)stats.half_count,
                    (unsigned long)stats.full_count,
                    (unsigned long)s_player_dma_both_event_count,
                    (unsigned long)stats.late_write_count);
  shellPrintEndLine(&shell, "  codec count=%lu max_tick=%lu write_max_tick=%lu\r\n",
                    (unsigned long)s_player_codec_read_count,
                    (unsigned long)s_player_codec_read_max_ticks,
                    (unsigned long)s_player_write_max_ticks);
  shellPrintEndLine(&shell, "  udr=%lu dma_err=%lu i2s_err=%lu last_err=0x%08lX last_sr=0x%08lX\r\n",
                    (unsigned long)stats.udr_count,
                    (unsigned long)stats.dma_error_count,
                    (unsigned long)stats.i2s_error_count,
                    (unsigned long)stats.last_error,
                    (unsigned long)stats.last_sr);
  shellPrintEndLine(&shell, "  task_stack_free=%lu\r\n",
                    (unsigned long)stack_space);
  shellPrintEndLine(&shell, "  mp3 sync=%lu decode=%lu main=%lu format=%lu recover=%lu conceal=%lu last=%ld\r\n",
                    (unsigned long)mp3_stats.sync_miss_count,
                    (unsigned long)mp3_stats.decode_error_count,
                    (unsigned long)mp3_stats.main_data_underflow_count,
                    (unsigned long)mp3_stats.format_mismatch_count,
                    (unsigned long)mp3_stats.recovery_count,
                    (unsigned long)mp3_stats.concealed_frame_count,
                    (long)mp3_stats.last_decode_error);
  shellPrintEndLine(&shell, "  mp3 bad rate=%lu channels=%u samples=%lu\r\n",
                    (unsigned long)mp3_stats.last_bad_sample_rate,
                    (unsigned int)mp3_stats.last_bad_channels,
                    (unsigned long)mp3_stats.last_bad_output_samps);
  shellPrintEndLine(&shell, "  mem ccm=%u/1000 ex=%u/1000\r\n",
                    (unsigned int)sys_mem_perused(SRAMCCM),
                    (unsigned int)sys_mem_perused(SRAMEX));
  return 0;
}

SHELL_EXPORT_CMD(
SHELL_CMD_PERMISSION(0) | SHELL_CMD_TYPE(SHELL_TYPE_CMD_MAIN),
audiostat, player_audio_stats_cmd, audio playback diagnostic stats);
