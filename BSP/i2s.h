#ifndef I2S__H
#define I2S__H

#include "stm32f4xx.h"
#include "sys.h"

typedef void (*audio_card_tx_callback_t)(void);
typedef void (*audio_card_tx_index_callback_t)(u8 idle_index, u32 sequence);

typedef struct audio_play_info
{
	uint32_t sample;
	uint8_t sample_bits;
	uint8_t channels;

	uint16_t volume;
	uint32_t pkg_size;
	uint32_t WAV_PLAY_BUFF_SIZE;

	uint8_t *wav_buffer;
	volatile uint8_t curr_buf_index;

	void *file;
	audio_card_tx_callback_t tx_callback;
	audio_card_tx_index_callback_t tx_index_callback;
	volatile u32 tx_sequence;
} audio_play_info_t;

typedef struct audio_card_stats
{
	u32 half_count;
	u32 full_count;
	u32 udr_count;
	u32 dma_error_count;
	u32 i2s_error_count;
	u32 late_write_count;
	u32 last_error;
	u32 last_sr;
} audio_card_stats_t;

int audio_card_init(int volume);
int audio_card_open(int sample_rate, int deepth, int channels, int pkg_size);
int audio_card_open_prepare(int sample_rate, int deepth, int channels, int pkg_size);
int audio_card_start(void);
int audio_card_pause(void);
int audio_card_resume(void);
int audio_card_get_freq(void);
int audio_card_chg_freq(int sample_rate);
int audio_card_chg_volume(int volume);
int audio_card_get_volume(void);
int audio_card_register_feeder(void (*feeding_func)(void));
void audio_card_register_tx(void (*callback)(void));
void audio_card_register_tx_index(audio_card_tx_index_callback_t callback);
int audio_card_write_to_buffer(uint8_t index, void *pkg_buff, int pkg_size);
int audio_card_write_silence(uint8_t index);
int audio_card_write(void *pkg_buff, int pkg_size);
int audio_card_close(void);
uint8_t *audio_card_get_buffer(uint8_t index);
uint32_t audio_card_get_pkg_size(void);
uint32_t audio_card_get_pcm_size(void);
uint8_t audio_card_get_idle_buffer_index(void);
void audio_card_get_stats(audio_card_stats_t *stats);
void audio_card_reset_stats(void);

#endif
