#include "i2s.h"
#include "Malloc.h"

extern I2S_HandleTypeDef hi2s3;

#define AUDIO_CARD_MEM_POOL      SRAMMAIN
#define AUDIO_CARD_MAX_VOLUME    100U
#define AUDIO_CARD_MIN_PKG_SIZE  16U

static audio_play_info_t apif;
static uint8_t audio_card_opened = 0;
static uint16_t audio_card_volume = AUDIO_CARD_MAX_VOLUME;
static volatile audio_card_stats_t audio_card_stats;

static uint32_t audio_card_map_freq(int sample_rate)
{
	switch (sample_rate)
	{
		case 8000:
			return I2S_AUDIOFREQ_8K;
		case 11000:
		case 11025:
			return I2S_AUDIOFREQ_11K;
		case 16000:
			return I2S_AUDIOFREQ_16K;
		case 22000:
		case 22050:
		case 22500:
			return I2S_AUDIOFREQ_22K;
		case 32000:
			return I2S_AUDIOFREQ_32K;
		case 44010:
		case 44100:
			return I2S_AUDIOFREQ_44K;
		case 48000:
			return I2S_AUDIOFREQ_48K;
		case 96000:
			return I2S_AUDIOFREQ_96K;
		case 192000:
			return I2S_AUDIOFREQ_192K;
		default:
			if (sample_rate >= (int)I2S_AUDIOFREQ_8K &&
			    sample_rate <= (int)I2S_AUDIOFREQ_192K)
			{
				return (uint32_t)sample_rate;
			}
			return 0;
	}
}

static uint32_t audio_card_calc_pcm_size(int sample_bits, int channels, int pkg_size)
{
	if (sample_bits == 16)
	{
		return (channels == 2) ? (uint32_t)pkg_size : (uint32_t)pkg_size * 2U;
	}

	if (sample_bits == 8)
	{
		return (channels == 2) ? (uint32_t)pkg_size * 2U : (uint32_t)pkg_size * 4U;
	}

	return 0;
}

static int16_t audio_card_read_le16(const uint8_t *src)
{
	return (int16_t)((uint16_t)src[0] | ((uint16_t)src[1] << 8));
}

static int16_t audio_card_scale_sample(int32_t sample)
{
	if (apif.volume >= AUDIO_CARD_MAX_VOLUME)
	{
		return (int16_t)sample;
	}
	if (apif.volume == 0U)
	{
		return 0;
	}

	return (int16_t)(sample * (int32_t)apif.volume / (int32_t)AUDIO_CARD_MAX_VOLUME);
}

static void audio_card_note_udr(void)
{
	if (__HAL_I2S_GET_FLAG(&hi2s3, I2S_FLAG_UDR) == SET)
	{
		audio_card_stats.udr_count++;
		audio_card_stats.last_sr = hi2s3.Instance->SR;
		__HAL_I2S_CLEAR_UDRFLAG(&hi2s3);
	}
}

static u8 audio_card_dma_active_index(void)
{
	u32 remain;
	u32 half_halfwords;

	if ((hi2s3.hdmatx == NULL) || (hi2s3.hdmatx->Instance == NULL) ||
	    (apif.WAV_PLAY_BUFF_SIZE == 0U))
	{
		return 0xFFU;
	}

	half_halfwords = apif.WAV_PLAY_BUFF_SIZE / 2U;
	remain = hi2s3.hdmatx->Instance->NDTR;

	return (remain > half_halfwords) ? 0U : 1U;
}

static void audio_card_pcm_8b_1ch(const uint8_t *src, int src_size, int16_t *dst)
{
	int i;
	int samples = src_size;

	if (samples > (int)apif.pkg_size)
	{
		samples = (int)apif.pkg_size;
	}

	for (i = 0; i < samples; i++)
	{
		int16_t sample = audio_card_scale_sample((int32_t)src[i] * 257L - 32768L);
		dst[i * 2] = sample;
		dst[i * 2 + 1] = sample;
	}
}

static void audio_card_pcm_8b_2ch(const uint8_t *src, int src_size, int16_t *dst)
{
	int i;
	int samples = src_size;

	if (samples > (int)apif.pkg_size)
	{
		samples = (int)apif.pkg_size;
	}

	for (i = 0; i < samples; i++)
	{
		dst[i] = audio_card_scale_sample((int32_t)src[i] * 257L - 32768L);
	}
}

static void audio_card_pcm_16b_1ch(const uint8_t *src, int src_size, int16_t *dst)
{
	int i;
	int samples = src_size / 2;
	int max_samples = (int)apif.pkg_size / 2;

	if (samples > max_samples)
	{
		samples = max_samples;
	}

	for (i = 0; i < samples; i++)
	{
		int16_t sample = audio_card_scale_sample(audio_card_read_le16(src + i * 2));
		dst[i * 2] = sample;
		dst[i * 2 + 1] = sample;
	}
}

static void audio_card_pcm_16b_2ch(const uint8_t *src, int src_size, int16_t *dst)
{
	int i;
	int samples = src_size / 2;
	int max_samples = (int)apif.pkg_size / 2;

	if (samples > max_samples)
	{
		samples = max_samples;
	}
	if (apif.volume >= AUDIO_CARD_MAX_VOLUME)
	{
		memcpy(dst, src, (size_t)samples * sizeof(int16_t));
		return;
	}

	for (i = 0; i < samples; i++)
	{
		dst[i] = audio_card_scale_sample(audio_card_read_le16(src + i * 2));
	}
}

static int audio_card_start_dma(void)
{
	uint32_t total_size = apif.WAV_PLAY_BUFF_SIZE * 2U;
	uint16_t total_halfwords = (uint16_t)(total_size / 2U);

	if (HAL_I2S_Init(&hi2s3) != HAL_OK)
	{
		return -1;
	}

	if (HAL_I2S_Transmit_DMA(&hi2s3, (uint16_t *)apif.wav_buffer, total_halfwords) != HAL_OK)
	{
		return -2;
	}
	__HAL_I2S_ENABLE_IT(&hi2s3, I2S_IT_ERR);

	return 0;
}

static void audio_card_notify(uint8_t current_buf_index)
{
	if (!audio_card_opened)
	{
		return;
	}

	audio_card_note_udr();
	if (current_buf_index == 1U)
	{
		audio_card_stats.half_count++;
	}
	else
	{
		audio_card_stats.full_count++;
	}

	apif.curr_buf_index = current_buf_index;

	if (apif.tx_callback != NULL)
	{
		apif.tx_callback();
	}
}

int audio_card_init(int volume)
{
	return audio_card_chg_volume(volume);
}

int audio_card_start(void)
{
	if (!audio_card_opened || (apif.wav_buffer == NULL))
	{
		return -1;
	}

	if (audio_card_start_dma() != 0)
	{
		audio_card_opened = 0;
		return -2;
	}

	return 0;
}

int audio_card_pause(void)
{
	if (!audio_card_opened || (apif.wav_buffer == NULL))
	{
		return -1;
	}

	if (HAL_I2S_DMAPause(&hi2s3) != HAL_OK)
	{
		return -2;
	}

	return 0;
}

int audio_card_resume(void)
{
	if (!audio_card_opened || (apif.wav_buffer == NULL))
	{
		return -1;
	}

	if (HAL_I2S_DMAResume(&hi2s3) != HAL_OK)
	{
		return -2;
	}

	return 0;
}

int audio_card_open_prepare(int sample_rate, int deepth, int channels, int pkg_size)
{
	uint32_t i2s_freq;
	uint32_t pcm_size;
	uint32_t total_size;
	void (*saved_callback)(void) = apif.tx_callback;

	if (audio_card_opened)
	{
		audio_card_close();
	}

	if (sample_rate < 2000)
	{
		return -1;
	}

	if ((channels != 1) && (channels != 2))
	{
		return -2;
	}

	if ((deepth != 8) && (deepth != 16))
	{
		return -3;
	}

	if (pkg_size < (int)AUDIO_CARD_MIN_PKG_SIZE)
	{
		return -4;
	}

	if ((deepth == 16) && ((pkg_size & 0x01) != 0))
	{
		return -5;
	}

	i2s_freq = audio_card_map_freq(sample_rate);
	if (i2s_freq == 0U)
	{
		return -6;
	}

	pcm_size = audio_card_calc_pcm_size(deepth, channels, pkg_size);
	total_size = pcm_size * 2U;
	if ((pcm_size == 0U) || ((pcm_size & 0x01U) != 0U) || ((total_size / 2U) > 0xFFFFU))
	{
		return -7;
	}

	memset(&apif, 0, sizeof(apif));
	audio_card_reset_stats();
	apif.sample = (uint32_t)sample_rate;
	apif.sample_bits = (uint8_t)deepth;
	apif.channels = (uint8_t)channels;
	apif.volume = audio_card_volume;
	apif.pkg_size = (uint32_t)pkg_size;
	apif.WAV_PLAY_BUFF_SIZE = pcm_size;
	apif.curr_buf_index = 0;
	apif.tx_callback = saved_callback;

	apif.wav_buffer = (uint8_t *)sys_malloc(AUDIO_CARD_MEM_POOL, total_size);
	if (apif.wav_buffer == NULL)
	{
		memset(&apif, 0, sizeof(apif));
		apif.tx_callback = saved_callback;
		apif.volume = audio_card_volume;
		return -8;
	}
	memset(apif.wav_buffer, 0, total_size);

	HAL_I2S_DMAStop(&hi2s3);
	hi2s3.Init.Mode = I2S_MODE_MASTER_TX;
	hi2s3.Init.Standard = I2S_STANDARD_PHILIPS;
	hi2s3.Init.DataFormat = I2S_DATAFORMAT_16B;
	hi2s3.Init.MCLKOutput = I2S_MCLKOUTPUT_DISABLE;
	hi2s3.Init.AudioFreq = i2s_freq;
	hi2s3.Init.CPOL = I2S_CPOL_LOW;
	hi2s3.Init.ClockSource = I2S_CLOCK_PLL;
#if defined(SPI_I2S_FULLDUPLEX_SUPPORT)
	hi2s3.Init.FullDuplexMode = I2S_FULLDUPLEXMODE_DISABLE;
#endif

	audio_card_opened = 1;

	return 0;
}

int audio_card_open(int sample_rate, int deepth, int channels, int pkg_size)
{
	int res = audio_card_open_prepare(sample_rate, deepth, channels, pkg_size);

	if (res != 0)
	{
		return res;
	}

	if (audio_card_start() != 0)
	{
		void (*saved_callback)(void) = apif.tx_callback;

		sys_free(AUDIO_CARD_MEM_POOL, apif.wav_buffer);
		memset(&apif, 0, sizeof(apif));
		apif.tx_callback = saved_callback;
		apif.volume = audio_card_volume;
		return -9;
	}

	return 0;
}

int audio_card_get_freq(void)
{
	return (int)apif.sample;
}

int audio_card_chg_freq(int sample_rate)
{
	uint32_t i2s_freq = audio_card_map_freq(sample_rate);

	if (i2s_freq == 0U)
	{
		return -1;
	}

	hi2s3.Init.AudioFreq = i2s_freq;
	apif.sample = (uint32_t)sample_rate;

	if (!audio_card_opened)
	{
		return 0;
	}

	HAL_I2S_DMAStop(&hi2s3);
	if (audio_card_start_dma() != 0)
	{
		audio_card_opened = 0;
		return -2;
	}

	return 0;
}

int audio_card_chg_volume(int volume)
{
	if (volume < 0)
	{
		volume = 0;
	}
	else if (volume > (int)AUDIO_CARD_MAX_VOLUME)
	{
		volume = (int)AUDIO_CARD_MAX_VOLUME;
	}

	audio_card_volume = (uint16_t)volume;
	apif.volume = (uint16_t)volume;

	return 0;
}

int audio_card_get_volume(void)
{
	return (int)audio_card_volume;
}

int audio_card_register_feeder(void (*feeding_func)(void))
{
	apif.tx_callback = feeding_func;
	return 0;
}

void audio_card_register_tx(void (*callback)(void))
{
	apif.tx_callback = callback;
}

int audio_card_write_to_buffer(uint8_t index, void *pkg_buff, int pkg_size)
{
	uint8_t *dst;

	if (!audio_card_opened || (apif.wav_buffer == NULL))
	{
		return -1;
	}

	if ((pkg_size < 0) || (pkg_size > (int)apif.pkg_size))
	{
		return -2;
	}

	if ((apif.sample_bits == 16) && ((pkg_size & 0x01) != 0))
	{
		return -3;
	}

	if ((pkg_size > 0) && (pkg_buff == NULL))
	{
		return -4;
	}

	if (index > 1U)
	{
		return -5;
	}

	dst = apif.wav_buffer + ((uint32_t)index * apif.WAV_PLAY_BUFF_SIZE);
	if ((hi2s3.Instance != NULL) &&
	    ((hi2s3.Instance->I2SCFGR & SPI_I2SCFGR_I2SE) != 0U) &&
	    (audio_card_dma_active_index() == index))
	{
		audio_card_stats.late_write_count++;
	}
	if (pkg_size < (int)apif.pkg_size)
	{
		memset(dst, 0, apif.WAV_PLAY_BUFF_SIZE);
	}

	if (pkg_size == 0)
	{
		return 0;
	}

	if (apif.sample_bits == 16)
	{
		if (apif.channels == 2)
		{
			audio_card_pcm_16b_2ch((const uint8_t *)pkg_buff, pkg_size, (int16_t *)dst);
		}
		else
		{
			audio_card_pcm_16b_1ch((const uint8_t *)pkg_buff, pkg_size, (int16_t *)dst);
		}
	}
	else if (apif.sample_bits == 8)
	{
		if (apif.channels == 2)
		{
			audio_card_pcm_8b_2ch((const uint8_t *)pkg_buff, pkg_size, (int16_t *)dst);
		}
		else
		{
			audio_card_pcm_8b_1ch((const uint8_t *)pkg_buff, pkg_size, (int16_t *)dst);
		}
	}
	else
	{
		return -6;
	}

	return 0;
}

int audio_card_write_silence(uint8_t index)
{
	uint8_t *dst;

	if (!audio_card_opened || (apif.wav_buffer == NULL))
	{
		return -1;
	}
	if (index > 1U)
	{
		return -2;
	}

	dst = apif.wav_buffer + ((uint32_t)index * apif.WAV_PLAY_BUFF_SIZE);
	if ((hi2s3.Instance != NULL) &&
	    ((hi2s3.Instance->I2SCFGR & SPI_I2SCFGR_I2SE) != 0U) &&
	    (audio_card_dma_active_index() == index))
	{
		audio_card_stats.late_write_count++;
	}
	memset(dst, 0, apif.WAV_PLAY_BUFF_SIZE);

	return 0;
}

int audio_card_write(void *pkg_buff, int pkg_size)
{
	return audio_card_write_to_buffer(audio_card_get_idle_buffer_index(), pkg_buff, pkg_size);
}

int audio_card_close(void)
{
	audio_card_opened = 0;
	__HAL_I2S_DISABLE_IT(&hi2s3, I2S_IT_ERR);
	HAL_I2S_DMAStop(&hi2s3);

	if (apif.wav_buffer != NULL)
	{
		sys_free(AUDIO_CARD_MEM_POOL, apif.wav_buffer);
	}

	memset(&apif, 0, sizeof(apif));
	apif.volume = audio_card_volume;

	return 0;
}

uint8_t *audio_card_get_buffer(uint8_t index)
{
	if ((apif.wav_buffer == NULL) || (index > 1U))
	{
		return NULL;
	}

	return apif.wav_buffer + ((uint32_t)index * apif.WAV_PLAY_BUFF_SIZE);
}

uint32_t audio_card_get_pkg_size(void)
{
	return apif.pkg_size;
}

uint32_t audio_card_get_pcm_size(void)
{
	return apif.WAV_PLAY_BUFF_SIZE;
}

uint8_t audio_card_get_idle_buffer_index(void)
{
	return (uint8_t)((apif.curr_buf_index + 1U) & 0x01U);
}

void audio_card_get_stats(audio_card_stats_t *stats)
{
	if (stats == NULL)
	{
		return;
	}

	stats->half_count = audio_card_stats.half_count;
	stats->full_count = audio_card_stats.full_count;
	stats->udr_count = audio_card_stats.udr_count;
	stats->dma_error_count = audio_card_stats.dma_error_count;
	stats->i2s_error_count = audio_card_stats.i2s_error_count;
	stats->late_write_count = audio_card_stats.late_write_count;
	stats->last_error = audio_card_stats.last_error;
	stats->last_sr = audio_card_stats.last_sr;
}

void audio_card_reset_stats(void)
{
	memset((void *)&audio_card_stats, 0, sizeof(audio_card_stats));
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
	if (hi2s == &hi2s3)
	{
		audio_card_notify(1U);
	}
}

void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s)
{
	if (hi2s == &hi2s3)
	{
		audio_card_notify(0U);
	}
}

void HAL_I2S_ErrorCallback(I2S_HandleTypeDef *hi2s)
{
	u32 err;

	if (hi2s != &hi2s3)
	{
		return;
	}

	err = HAL_I2S_GetError(hi2s);
	audio_card_stats.i2s_error_count++;
	audio_card_stats.last_error = err;
	audio_card_stats.last_sr = hi2s->Instance->SR;

	if ((err & HAL_I2S_ERROR_DMA) != 0U)
	{
		audio_card_stats.dma_error_count++;
	}
	if ((err & HAL_I2S_ERROR_UDR) != 0U)
	{
		audio_card_stats.udr_count++;
		__HAL_I2S_CLEAR_UDRFLAG(hi2s);
	}
}

void SPI3_IRQHandler(void)
{
	HAL_I2S_IRQHandler(&hi2s3);
}
