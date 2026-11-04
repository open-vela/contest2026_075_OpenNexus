/* SPDX-License-Identifier: Apache-2.0
 * Bounded SF32LB52-DevKit-LCD onboard analog microphone diagnostic.
 * Clock tables/order follow OpenSiFli SiFli-SDK drv_audcodec_m.c at
 * a3d720121c2c66cb6cffa758e90191db3340ad61. This is a HAL bring-up tool,
 * not the openvela audio lower-half or an ASR implementation.
 */
#include <nuttx/config.h>
#include <nuttx/cache.h>
#include <errno.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "bf0_hal.h"
#include "bf0_hal_audcodec.h"

#define RATE 16000u
#define SECONDS 3u
#define WARM_SAMPLES (RATE / 4u)
#define KEEP_SAMPLES (RATE * SECONDS)
#define ALL_SAMPLES (WARM_SAMPLES + KEEP_SAMPLES)

/* Internal SRAM, aligned and isolated for DMA/cache maintenance. Two signed
 * 16-bit samples per 32-bit DMA word (ADC/DAC DATA_FORMAT=1 in this HAL).
 */
static uint32_t raw[ALL_SAMPLES / 2u] __attribute__((aligned(32)));
static AUDCODEC_HandleTypeDef codec;
static DMA_HandleTypeDef dma;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static int valid;
static AUDCODE_ADC_CLK_CONFIG_TYPE adc_clock = {16000, 0, 10, 1, 0, 0, 5, 2};
static AUDCODE_DAC_CLK_CONFIG_TYPE dac_clock =
  {16000, 0, 1, 4, 0x14d, 0, 5, 4, 2, 20, 20, 0};
extern void BSP_GPIO_Set(int pin, int val, int is_porta);
/* This vendor HAL C source defines ADC volume but its header omits it. */
extern HAL_StatusTypeDef HAL_AUDCODEC_Config_ADCPath_Volume(
  AUDCODEC_HandleTypeDef *handle, int channel, int volume);

static uint64_t monotonic_ms(void)
{
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  return (uint64_t)t.tv_sec * 1000u + t.tv_nsec / 1000000u;
}

static uint8_t *pcm_bytes(void)
{
  return (uint8_t *)raw + WARM_SAMPLES * sizeof(int16_t);
}

static uint32_t pcm_hash(void)
{
  uint32_t hash = UINT32_C(2166136261);
  const uint8_t *p = pcm_bytes();
  for (unsigned i = 0; i < KEEP_SAMPLES * 2u; ++i)
    hash = (hash ^ p[i]) * UINT32_C(16777619);
  return hash;
}

static void stats(void)
{
  const int16_t *p = (const int16_t *)pcm_bytes();
  for (unsigned block = 0; block < SECONDS * 2u; ++block) {
    int peak = 0;
    unsigned long abs_sum = 0, clipped = 0;
    for (unsigned i = 0; i < RATE / 2u; ++i) {
      int v = p[block * (RATE / 2u) + i];
      int a = v < 0 ? -v : v;
      if (a > peak) peak = a;
      abs_sum += a;
      if (a >= 32760) ++clipped;
    }
    printf("mic_test: block=%u mean_abs=%lu peak=%d clipped=%lu\n",
           block, abs_sum / (RATE / 2u), peak, clipped);
  }
  printf("mic_test: PCM s16le mono rate=%u samples=%u fnv1a=%08" PRIx32 "\n",
         RATE, KEEP_SAMPLES, pcm_hash());
}

static int hardware_init(int recording)
{
  if ((DMA1_Channel8->CCR & DMAC_CCR8_EN) ||
      (hwp_audcodec->CFG & (AUDCODEC_CFG_ADC_ENABLE | AUDCODEC_CFG_DAC_ENABLE))) {
    puts("mic_test: audio or DMA1 channel8 busy; refused");
    return -EBUSY;
  }
  BSP_GPIO_Set(10, 0, 1);
  HAL_PMU_EnableAudio(1);
  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC);
  HAL_TURN_ON_PLL();
  memset(&codec, 0, sizeof(codec));
  memset(&dma, 0, sizeof(dma));
  codec.Instance = hwp_audcodec;
  codec.Init.adc_cfg.opmode = 1;
  codec.Init.adc_cfg.adc_clk = &adc_clock;
  codec.Init.dac_cfg.opmode = 1;
  codec.Init.dac_cfg.dac_clk = &dac_clock;
  dma.Instance = DMA1_Channel8;
  dma.Init.Request = recording ? 39 : 41;
  codec.hdma[recording ? HAL_AUDCODEC_ADC_CH0 : HAL_AUDCODEC_DAC_CH0] = &dma;
  int ret = HAL_AUDCODEC_Init(&codec);
  if (ret != HAL_OK) return -EIO;
  /* The codec HAL defaults to circular DMA. This diagnostic uses a finite
   * transfer and polls completion; no refill ISR or NVIC changes are needed.
   */
  dma.Init.Mode = DMA_NORMAL;
  if (HAL_DMA_Init(&dma) != HAL_OK) return -EIO;
  return 0;
}

static void hardware_stop(int recording)
{
  BSP_GPIO_Set(10, 0, 1);
  unsigned channel = recording ? HAL_AUDCODEC_ADC_CH0 : HAL_AUDCODEC_DAC_CH0;
  HAL_AUDCODEC_DMAStop(&codec, channel);
  if (recording) {
    __HAL_AUDCODEC_ADC_DISABLE(&codec);
    __HAL_AUDCODEC_ADC_CH0_DISABLE(&codec);
    HAL_AUDCODEC_Close_Analog_ADCPath();
  } else {
    __HAL_AUDCODEC_DAC_DISABLE(&codec);
    __HAL_AUDCODEC_DAC_CH0_DISABLE(&codec);
    HAL_AUDCODEC_Close_Analog_DACPath();
  }
  HAL_AUDCODEC_DeInit(&codec);
  HAL_TURN_OFF_PLL();
}

static int wait_dma(unsigned timeout_ms)
{
  uint64_t start = monotonic_ms();
  while (DMA1_Channel8->CNDTR) {
    if (hwp_dmac1->ISR & DMAC_ISR_TEIF8) return -EIO;
    if (monotonic_ms() - start > timeout_ms) return -ETIMEDOUT;
    usleep(10000);
  }
  if (hwp_dmac1->ISR & DMAC_ISR_TEIF8) return -EIO;
  printf("mic_test: DMA complete elapsed=%" PRIu64 "ms remaining=0\n",
         monotonic_ms() - start);
  return 0;
}

static int record_pcm(int gain_db)
{
  int ret = hardware_init(1);
  if (ret == -EBUSY) return ret;
  if (ret < 0) goto out;
  valid = 0;
  memset(raw, 0, sizeof(raw));
  up_clean_dcache((uintptr_t)raw, (uintptr_t)(raw + ALL_SAMPLES / 2u));
  if (HAL_AUDCODEC_Config_RChanel(&codec, 0, &codec.Init.adc_cfg) != HAL_OK ||
      HAL_AUDCODEC_Config_ADCPath_Volume(&codec, 0, gain_db) != HAL_OK) {
    ret = -EIO;
    goto out;
  }
  /* CONFIG_RChanel selects packed PCM16; preserve that format for playback. */
  HAL_AUDCODEC_Config_Analog_ADCPath(&adc_clock);
  ret = HAL_DMA_Start(&dma, (uintptr_t)&hwp_audcodec->ADC_CH0_ENTRY,
                      (uintptr_t)raw, ALL_SAMPLES / 2u);
  if (ret != HAL_OK) { ret = -EIO; goto out; }
  __HAL_DMA_DISABLE_IT(&dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  hwp_audcodec->ADC_CH0_CFG |= AUDCODEC_ADC_CH0_CFG_DMA_EN;
  printf("mic_test: RECORD now, 3 seconds (+250ms warmup), gain=%ddB\n", gain_db);
  fflush(stdout);
  __HAL_AUDCODEC_ADC_ENABLE(&codec);
  ret = wait_dma(5000);
out:
  printf("mic_test: capture result=%d remaining=%lu ISR=%08lx ADC_CFG=%08lx CH0=%08lx\n",
         ret, (unsigned long)DMA1_Channel8->CNDTR, (unsigned long)hwp_dmac1->ISR,
         (unsigned long)hwp_audcodec->ADC_CFG, (unsigned long)hwp_audcodec->ADC_CH0_CFG);
  hardware_stop(1);
  if (ret == 0) {
    up_invalidate_dcache((uintptr_t)raw, (uintptr_t)(raw + ALL_SAMPLES / 2u));
    valid = 1;
    stats();
  }
  return ret;
}

static int play_pcm(void)
{
  if (!valid) { puts("mic_test: no successful recording"); return -ENODATA; }
  int ret = hardware_init(0);
  if (ret == -EBUSY) return ret;
  if (ret < 0) goto out;
  up_clean_dcache((uintptr_t)pcm_bytes(), (uintptr_t)pcm_bytes() + KEEP_SAMPLES * 2u);
  if (HAL_AUDCODEC_Config_TChanel(&codec, 0, &codec.Init.dac_cfg) != HAL_OK ||
      HAL_AUDCODEC_Config_DACPath_Volume(&codec, 0, -18) != HAL_OK) {
    ret = -EIO; goto out;
  }
  ret = HAL_DMA_Start(&dma, (uintptr_t)pcm_bytes(),
                      (uintptr_t)&hwp_audcodec->DAC_CH0_ENTRY, KEEP_SAMPLES / 2u);
  if (ret != HAL_OK) { ret = -EIO; goto out; }
  __HAL_DMA_DISABLE_IT(&dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  hwp_audcodec->DAC_CH0_CFG |= AUDCODEC_DAC_CH0_CFG_DMA_EN;
  HAL_AUDCODEC_Config_DACPath(&codec, 1);
  ret = HAL_AUDCODEC_Config_Analog_DACPath(&dac_clock);
  if (ret != HAL_OK) { ret = -EIO; goto out; }
  __HAL_AUDCODEC_DAC_ENABLE(&codec);
  HAL_AUDCODEC_Config_DACPath(&codec, 0);
  BSP_GPIO_Set(10, 1, 1);
  puts("mic_test: PLAY recorded microphone PCM for 3 seconds (-18dB)");
  fflush(stdout);
  ret = wait_dma(5000);
out:
  hardware_stop(0);
  printf("mic_test: playback result=%d; listener confirmation required\n", ret);
  return ret;
}

static int dump_pcm(void)
{
  static const char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  if (!valid) return -ENODATA;
  const uint8_t *p = pcm_bytes();
  size_t bytes = KEEP_SAMPLES * 2u;
  printf("MIC_PCM_BEGIN samples=%u rate=%u bytes=%u fnv1a=%08" PRIx32 "\n",
         KEEP_SAMPLES, RATE, (unsigned)bytes, pcm_hash());
  for (size_t offset = 0; offset < bytes; offset += 48) {
    size_t n = bytes - offset < 48 ? bytes - offset : 48;
    char line[65]; size_t used = 0;
    for (size_t j = 0; j < n; j += 3) {
      unsigned a = p[offset+j];
      unsigned b = j+1 < n ? p[offset+j+1] : 0;
      unsigned c = j+2 < n ? p[offset+j+2] : 0;
      line[used++] = alphabet[a>>2];
      line[used++] = alphabet[((a&3)<<4)|(b>>4)];
      line[used++] = j+1 < n ? alphabet[((b&15)<<2)|(c>>6)] : '=';
      line[used++] = j+2 < n ? alphabet[c&63] : '=';
    }
    line[used] = 0; puts(line);
  }
  puts("MIC_PCM_END");
  return 0;
}

int main(int argc, char **argv)
{
  if (pthread_mutex_trylock(&lock) != 0) return 1;
  int ret = -EINVAL;
  if (argc == 2 && !strcmp(argv[1], "record")) ret = record_pcm(0);
  else if (argc == 3 && !strcmp(argv[1], "record")) {
    char *end; long gain = strtol(argv[2], &end, 10);
    if (*argv[2] && !*end && gain >= -12 && gain <= 24) ret = record_pcm((int)gain);
  } else if (argc == 2 && !strcmp(argv[1], "play")) ret = play_pcm();
  else if (argc == 2 && !strcmp(argv[1], "dump")) ret = dump_pcm();
  else if (argc == 2 && !strcmp(argv[1], "stats")) {
    if (valid) { stats(); ret = 0; } else ret = -ENODATA;
  }
  if (ret == -EINVAL)
    puts("Usage: mic_test record [gain_db:-12..24] | play | stats | dump");
  if (ret < 0) printf("mic_test: error=%d\n", ret);
  pthread_mutex_unlock(&lock);
  return ret < 0 ? 1 : 0;
}
