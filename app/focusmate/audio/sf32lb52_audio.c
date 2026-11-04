/* SPDX-License-Identifier: Apache-2.0
 * SF32LB52-DevKit-LCD, PCM16 mono 16 kHz openvela audio lower halves.
 * ADC/DAC clocks follow the previously verified OpenSiFli HAL diagnostic.
 * DMA1 CH8 uses a small SRAM ring; clients own their normal audio buffers.
 */
#include <nuttx/config.h>
#include <nuttx/audio/audio.h>
#include <nuttx/arch.h>
#include <nuttx/cache.h>
#include <nuttx/clock.h>
#include <nuttx/irq.h>
#include <nuttx/mutex.h>
#include <nuttx/semaphore.h>
#include <nuttx/signal.h>
#include <nuttx/spinlock.h>
#include <nuttx/queue.h>
#include <pthread.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "bf0_hal.h"
#include "bf0_hal_audcodec.h"

#if defined(CONFIG_AUDIO_MULTI_SESSION) || defined(CONFIG_AUDIO_EXCLUDE_STOP)
#error "FocusMate audio requires single-session audio with STOP enabled"
#endif
#define PERIOD 2048u
#define RING_BYTES (PERIOD * 2u)
#define AUDIO_IRQ (NVIC_IRQ_FIRST + DMAC1_CH8_IRQn)

struct fm_audio_s {
  struct audio_lowerhalf_s lower;
  dq_queue_t queue;
  sem_t signal;
  pthread_t thread;
  bool capture, reserved, configured, created;
  volatile bool running, stopping;
  volatile unsigned pending;
  volatile int fault;
  bool final[2];
  unsigned warmup;
  unsigned long transferred;
};
static struct fm_audio_s capture_dev, playback_dev;
static mutex_t hw_lock = NXMUTEX_INITIALIZER;
static struct fm_audio_s *volatile owner;
static AUDCODEC_HandleTypeDef codec;
static DMA_HandleTypeDef dma;
static uint32_t ring[RING_BYTES / 4] __attribute__((aligned(32)));
static AUDCODE_ADC_CLK_CONFIG_TYPE adc_clock = {16000,0,10,1,0,0,5,2};
static AUDCODE_DAC_CLK_CONFIG_TYPE dac_clock =
  {16000,0,1,4,0x14d,0,5,4,2,20,20,0};
extern void BSP_GPIO_Set(int pin, int value, int porta);
extern HAL_StatusTypeDef HAL_AUDCODEC_Config_ADCPath_Volume(
  AUDCODEC_HandleTypeDef *handle, int channel, int volume);

static void notify(struct fm_audio_s *p, unsigned reason,
                   struct ap_buffer_s *apb, int status)
{
  if (p->lower.upper)
    p->lower.upper(p->lower.priv, reason, apb, status < 0 ? -status : status);
}
static void returned(struct fm_audio_s *p, struct ap_buffer_s *apb, int status)
{
  notify(p, AUDIO_CALLBACK_DEQUEUE, apb, status);
  apb_free(apb);
}
static struct ap_buffer_s *pop(struct fm_audio_s *p)
{
  irqstate_t f = enter_critical_section();
  struct ap_buffer_s *apb = (struct ap_buffer_s *)dq_remfirst(&p->queue);
  leave_critical_section(f);
  return apb;
}
static void flush(struct fm_audio_s *p, int status)
{
  struct ap_buffer_s *apb;
  while ((apb = pop(p)) != NULL) {
    int result = p->capture && apb->nbytes ? 0 : status;
    apb->curbyte = 0;
    returned(p, apb, result);
  }
}

static int input_period(struct fm_audio_s *p, unsigned half)
{
  if (p->warmup) { p->warmup--; return 0; }
  uint8_t *src = (uint8_t *)ring + half * PERIOD;
  up_invalidate_dcache((uintptr_t)src, (uintptr_t)src + PERIOD);
  unsigned used = 0;
  while (used < PERIOD) {
    irqstate_t f = enter_critical_section();
    struct ap_buffer_s *apb = (struct ap_buffer_s *)dq_peek(&p->queue);
    if (!apb) { leave_critical_section(f); return -ENOBUFS; }
    unsigned count = apb->nmaxbytes - apb->nbytes;
    if (count > PERIOD - used) count = PERIOD - used;
    memcpy(apb->samp + apb->nbytes, src + used, count);
    apb->nbytes += count;
    used += count;
    bool done = apb->nbytes == apb->nmaxbytes;
    if (done) dq_remfirst(&p->queue);
    leave_critical_section(f);
    if (done) returned(p, apb, 0);
  }
  p->transferred += used;
  return 0;
}
static int output_period(struct fm_audio_s *p, unsigned half)
{
  uint8_t *dst = (uint8_t *)ring + half * PERIOD;
  unsigned used = 0;
  memset(dst, 0, PERIOD);
  p->final[half] = false;
  while (used < PERIOD) {
    irqstate_t f = enter_critical_section();
    struct ap_buffer_s *apb = (struct ap_buffer_s *)dq_peek(&p->queue);
    if (!apb) {
      leave_critical_section(f);
      if (p->final[half ^ 1]) { p->final[half] = true; break; }
      return -ENOBUFS;
    }
    unsigned count = apb->nbytes - apb->curbyte;
    if (count > PERIOD - used) count = PERIOD - used;
    memcpy(dst + used, apb->samp + apb->curbyte, count);
    apb->curbyte += count;
    used += count;
    bool done = apb->curbyte == apb->nbytes;
    bool final = done && (apb->flags & AUDIO_APB_FINAL);
    if (done) dq_remfirst(&p->queue);
    leave_critical_section(f);
    if (done) returned(p, apb, 0);
    if (final) { p->final[half] = true; break; }
  }
  up_clean_dcache((uintptr_t)dst, (uintptr_t)dst + PERIOD);
  p->transferred += used;
  return 0;
}

static int dma_interrupt(int irq, void *context, void *arg)
{
  (void)irq; (void)context; (void)arg;
  uint32_t flags = hwp_dmac1->ISR;
  hwp_dmac1->IFCR = DMAC_IFCR_CGIF8;
  struct fm_audio_s *p = owner;
  if (!p || !p->running) return 0;
  unsigned bits = ((flags & DMAC_ISR_HTIF8) ? 1u : 0u) |
                  ((flags & DMAC_ISR_TCIF8) ? 2u : 0u);
  if ((flags & DMAC_ISR_TEIF8) || (p->pending & bits) || bits == 3u)
    p->fault = -EOVERFLOW;
  p->pending |= bits;
  nxsem_post(&p->signal);
  return 0;
}
static int hardware_prepare(struct fm_audio_s *p)
{
  if ((DMA1_Channel8->CCR & DMAC_CCR8_EN) ||
      (hwp_audcodec->CFG & (AUDCODEC_CFG_ADC_ENABLE | AUDCODEC_CFG_DAC_ENABLE)))
    return -EBUSY;
  memset(&codec, 0, sizeof(codec)); memset(&dma, 0, sizeof(dma));
  memset(ring, 0, sizeof(ring));
  BSP_GPIO_Set(10, 0, 1);
  HAL_PMU_EnableAudio(1); HAL_RCC_EnableModule(RCC_MOD_AUDCODEC);
  HAL_TURN_ON_PLL();
  codec.Instance = hwp_audcodec;
  codec.Init.adc_cfg.opmode = 1; codec.Init.adc_cfg.adc_clk = &adc_clock;
  codec.Init.dac_cfg.opmode = 1; codec.Init.dac_cfg.dac_clk = &dac_clock;
  dma.Instance = DMA1_Channel8; dma.Init.Request = p->capture ? 39 : 41;
  /* The HAL programs NVIC itself during both allocation and Start. A zeroed
   * IrqPrio would override openvela's equal-priority, non-nested IRQ setup.
   * CMSIS takes the unshifted priority, unlike the NuttX priority constant. */
  dma.Init.IrqPrio = NVIC_SYSH_PRIORITY_DEFAULT >> (8 - __NVIC_PRIO_BITS);
  codec.hdma[p->capture ? HAL_AUDCODEC_ADC_CH0 : HAL_AUDCODEC_DAC_CH0] = &dma;
  if (HAL_AUDCODEC_Init(&codec) != HAL_OK) return -EIO;
  if (dma.Instance != DMA1_Channel8) return -EIO;
  if (p->capture) {
    if (HAL_AUDCODEC_Config_RChanel(&codec,0,&codec.Init.adc_cfg) != HAL_OK)
      return -EIO;
    HAL_AUDCODEC_Config_ADCPath_Volume(&codec, 0, 12);
    HAL_AUDCODEC_Config_Analog_ADCPath(&adc_clock);
  } else {
    if (HAL_AUDCODEC_Config_TChanel(&codec,0,&codec.Init.dac_cfg) != HAL_OK)
      return -EIO;
    HAL_AUDCODEC_Config_DACPath_Volume(&codec,0,-6);
    HAL_AUDCODEC_Config_DACPath(&codec,1);
    if (HAL_AUDCODEC_Config_Analog_DACPath(&dac_clock) != HAL_OK) return -EIO;
  }
  return 0;
}
static int hardware_start(struct fm_audio_s *p)
{
  up_clean_dcache((uintptr_t)ring, (uintptr_t)ring + sizeof(ring));
  uintptr_t entry = p->capture ? (uintptr_t)&hwp_audcodec->ADC_CH0_ENTRY :
                                (uintptr_t)&hwp_audcodec->DAC_CH0_ENTRY;
  int ret = HAL_DMA_Start(&dma, p->capture ? entry : (uintptr_t)ring,
                              p->capture ? (uintptr_t)ring : entry,
                              RING_BYTES / 4);
  if (ret != HAL_OK) return -EIO;
  if (dma.Instance != DMA1_Channel8) { HAL_DMA_Abort(&dma); return -EBUSY; }
  hwp_dmac1->IFCR = DMAC_IFCR_CGIF8;
  __HAL_DMA_ENABLE_IT(&dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  up_enable_irq(AUDIO_IRQ);
  if (p->capture) {
    hwp_audcodec->ADC_CH0_CFG |= AUDCODEC_ADC_CH0_CFG_DMA_EN;
    __HAL_AUDCODEC_ADC_ENABLE(&codec);
  } else {
    hwp_audcodec->DAC_CH0_CFG |= AUDCODEC_DAC_CH0_CFG_DMA_EN;
    __HAL_AUDCODEC_DAC_ENABLE(&codec);
    HAL_AUDCODEC_Config_DACPath(&codec,0);
    BSP_GPIO_Set(10,1,1);
  }
  return 0;
}
static void hardware_stop(struct fm_audio_s *p)
{
  up_disable_irq(AUDIO_IRQ);
  __HAL_DMA_DISABLE_IT(&dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  BSP_GPIO_Set(10,0,1);
  /* Stop peripheral requests before releasing the circular DMA channel. */
  if (p->capture) {
    hwp_audcodec->ADC_CH0_CFG &= ~AUDCODEC_ADC_CH0_CFG_DMA_EN;
    __HAL_AUDCODEC_ADC_DISABLE(&codec);
    __HAL_AUDCODEC_ADC_CH0_DISABLE(&codec);
  } else {
    hwp_audcodec->DAC_CH0_CFG &= ~AUDCODEC_DAC_CH0_CFG_DMA_EN;
    __HAL_AUDCODEC_DAC_DISABLE(&codec);
    __HAL_AUDCODEC_DAC_CH0_DISABLE(&codec);
  }
  HAL_AUDCODEC_DMAStop(&codec,p->capture ? HAL_AUDCODEC_ADC_CH0 : HAL_AUDCODEC_DAC_CH0);
  if (p->capture) HAL_AUDCODEC_Close_Analog_ADCPath();
  else HAL_AUDCODEC_Close_Analog_DACPath();
  HAL_AUDCODEC_DeInit(&codec); HAL_TURN_OFF_PLL();
  hwp_dmac1->IFCR = DMAC_IFCR_CGIF8;
}

static void *audio_worker(void *arg)
{
  struct fm_audio_s *p = arg;
  int result = 0;
  while (!p->stopping) {
    int ret = nxsem_tickwait_uninterruptible(&p->signal, MSEC2TICK(500));
    if (p->stopping) break;
    if (ret < 0) { result = ret; break; }
    irqstate_t f = enter_critical_section();
    unsigned bits = p->pending; p->pending = 0;
    int fault = p->fault;
    leave_critical_section(f);
    if (fault || bits == 3u) { result = fault ? fault : -EOVERFLOW; break; }
    for (unsigned half = 0; half < 2; ++half) if (bits & (1u << half)) {
      if (!p->capture && p->final[half]) { p->stopping = true; break; }
      ret = p->capture ? input_period(p,half) : output_period(p,half);
      if (ret < 0) { result = ret; p->stopping = true; break; }
    }
  }
  hardware_stop(p);
  flush(p, result ? result : -ECANCELED);
  if (result) notify(p,AUDIO_CALLBACK_IOERR,NULL,result);
  nxmutex_lock(&hw_lock);
  if (owner == p) owner = NULL;
  p->running = false;
  nxmutex_unlock(&hw_lock);
  printf("[board-audio] %s stopped bytes=%lu result=%d\n",
         p->capture ? "capture" : "playback",p->transferred,result);
  notify(p,AUDIO_CALLBACK_COMPLETE,NULL,result);
  return NULL;
}
static int fm_stop(struct audio_lowerhalf_s *lower)
{
  struct fm_audio_s *p = (struct fm_audio_s *)lower;
  p->stopping = true;
  nxsem_post(&p->signal);
  if (p->created) {
    pthread_join(p->thread,NULL);
    p->created = false;
  } else flush(p,-ECANCELED);
  return 0;
}
static int fm_start(struct audio_lowerhalf_s *lower)
{
  struct fm_audio_s *p = (struct fm_audio_s *)lower;
  if (!p->reserved || !p->configured) return -EPERM;
  if (p->running) return -EBUSY;
  if (p->created) { pthread_join(p->thread,NULL); p->created = false; }
  nxmutex_lock(&hw_lock);
  if (owner) { nxmutex_unlock(&hw_lock); return -EBUSY; }
  int ret = hardware_prepare(p);
  if (ret < 0) {
    if (ret != -EBUSY) hardware_stop(p);
    nxmutex_unlock(&hw_lock); return ret;
  }
  owner = p;
  p->stopping = false; p->fault = 0; p->pending = 0;
  p->warmup = p->capture ? 4 : 0; p->transferred = 0;
  p->final[0] = p->final[1] = false;
  while (nxsem_trywait(&p->signal) == 0) { }
  if (!p->capture) {
    ret = output_period(p,0);
    if (!ret) ret = output_period(p,1);
    if (ret < 0) goto fail;
  }
  pthread_attr_t attr;
  pthread_attr_init(&attr); pthread_attr_setstacksize(&attr,4096);
  struct sched_param priority = {.sched_priority = 120};
  pthread_attr_setinheritsched(&attr,PTHREAD_EXPLICIT_SCHED);
  pthread_attr_setschedparam(&attr,&priority);
  p->running = true;
  ret = pthread_create(&p->thread,&attr,audio_worker,p);
  pthread_attr_destroy(&attr);
  if (ret) { ret = -ret; p->running = false; goto fail; }
  p->created = true;
  ret = hardware_start(p);
  if (ret < 0) {
    p->fault = ret; p->stopping = true; nxsem_post(&p->signal);
    nxmutex_unlock(&hw_lock); fm_stop(lower); return ret;
  }
  nxmutex_unlock(&hw_lock);
  printf("[board-audio] %s started PCM16/16000/mono ring=%u IRQ=%d priority=%lu\n",
         p->capture ? "capture" : "playback",RING_BYTES,AUDIO_IRQ,
         (unsigned long)NVIC_GetPriority(DMAC1_CH8_IRQn));
  return 0;
fail:
  hardware_stop(p); owner = NULL;
  nxmutex_unlock(&hw_lock);
  return ret;
}
static int fm_getcaps(struct audio_lowerhalf_s *lower,int type,
                      struct audio_caps_s *caps)
{
  (void)type;
  struct fm_audio_s *p = (struct fm_audio_s *)lower;
  caps->ac_channels = 1; caps->ac_format.hw = 0; caps->ac_controls.w = 0;
  if (caps->ac_type == AUDIO_TYPE_QUERY) {
    caps->ac_controls.b[0] = p->capture ? AUDIO_TYPE_INPUT : AUDIO_TYPE_OUTPUT;
    caps->ac_format.hw = 1u << (AUDIO_FMT_PCM - 1);
  } else if (caps->ac_type == (p->capture ? AUDIO_TYPE_INPUT : AUDIO_TYPE_OUTPUT)) {
    caps->ac_controls.hw[0] = AUDIO_SAMP_RATE_16K;
    caps->ac_controls.b[2] = 16;
  }
  return caps->ac_len;
}
static int fm_configure(struct audio_lowerhalf_s *lower,
                        const struct audio_caps_s *caps)
{
  struct fm_audio_s *p = (struct fm_audio_s *)lower;
  unsigned rate = caps->ac_controls.hw[0] | ((unsigned)caps->ac_controls.b[3] << 16);
  if (p->running) return -EBUSY;
  if (caps->ac_type != (p->capture ? AUDIO_TYPE_INPUT : AUDIO_TYPE_OUTPUT) ||
      caps->ac_channels != 1 || rate != 16000 || caps->ac_controls.b[2] != 16 ||
      (caps->ac_subtype && caps->ac_subtype != AUDIO_FMT_PCM)) return -EINVAL;
  p->configured = true;
  return 0;
}
static int fm_enqueue(struct audio_lowerhalf_s *lower,struct ap_buffer_s *apb)
{
  struct fm_audio_s *p = (struct fm_audio_s *)lower;
  if (!apb || !apb->samp || !apb->nmaxbytes || (apb->nmaxbytes & 3u) ||
      (!p->capture && (!apb->nbytes || (apb->nbytes & 3u) || apb->nbytes > apb->nmaxbytes)))
    return -EINVAL;
  apb_reference(apb);
  if (p->capture) apb->nbytes = 0;
  apb->curbyte = 0;
  irqstate_t f = enter_critical_section();
  dq_addlast(&apb->dq_entry,&p->queue);
  leave_critical_section(f);
  return 0;
}
static int fm_cancel(struct audio_lowerhalf_s *lower,struct ap_buffer_s *apb)
{
  struct fm_audio_s *p = (struct fm_audio_s *)lower;
  irqstate_t f = enter_critical_section();
  struct dq_entry_s *entry;
  for (entry = dq_peek(&p->queue); entry; entry = dq_next(entry))
    if (entry == &apb->dq_entry) break;
  if (entry) dq_rem(entry,&p->queue);
  leave_critical_section(f);
  if (!entry) return -ENOENT;
  returned(p,apb,-ECANCELED);
  return 0;
}
static int fm_ioctl(struct audio_lowerhalf_s *lower,int command,unsigned long arg)
{
  (void)lower;
  if (command == AUDIOIOC_GETBUFFERINFO) {
    if (!arg) return -EINVAL;
    struct ap_buffer_info_s *info = (struct ap_buffer_info_s *)arg;
    info->nbuffers = 4; info->buffer_size = 2048;
    return 0;
  }
  return -ENOTTY;
}
static int fm_reserve(struct audio_lowerhalf_s *lower)
{
  struct fm_audio_s *p = (struct fm_audio_s *)lower;
  nxmutex_lock(&hw_lock);
  int ret = p->reserved ? -EBUSY : 0;
  if (!ret) p->reserved = true;
  nxmutex_unlock(&hw_lock);
  return ret;
}
static int fm_release(struct audio_lowerhalf_s *lower)
{
  struct fm_audio_s *p = (struct fm_audio_s *)lower;
  fm_stop(lower);
  nxmutex_lock(&hw_lock);
  p->reserved = false; p->configured = false;
  nxmutex_unlock(&hw_lock);
  return 0;
}
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
static int fm_pause(struct audio_lowerhalf_s *lower)
{ (void)lower; return -ENOTSUP; }
#endif
static const struct audio_ops_s ops = {
  .getcaps = fm_getcaps, .configure = fm_configure, .shutdown = fm_stop,
  .start = fm_start, .stop = fm_stop,
#ifndef CONFIG_AUDIO_EXCLUDE_PAUSE_RESUME
  .pause = fm_pause, .resume = fm_pause,
#endif
  .enqueuebuffer = fm_enqueue, .cancelbuffer = fm_cancel,
  .ioctl = fm_ioctl, .reserve = fm_reserve, .release = fm_release,
};
int focus_board_audio_initialize(void)
{
  int ret = irq_attach(AUDIO_IRQ,dma_interrupt,NULL);
  if (ret < 0) return ret;
  up_disable_irq(AUDIO_IRQ);
  capture_dev.capture = true;
  capture_dev.lower.ops = playback_dev.lower.ops = &ops;
  dq_init(&capture_dev.queue); dq_init(&playback_dev.queue);
  nxsem_init(&capture_dev.signal,0,0); nxsem_init(&playback_dev.signal,0,0);
  ret = audio_register("pcm0c",&capture_dev.lower);
  if (!ret) ret = audio_register("pcm0p",&playback_dev.lower);
  printf("[board-audio] /dev/audio/pcm0c /dev/audio/pcm0p register=%d\n",ret);
  return ret;
}
