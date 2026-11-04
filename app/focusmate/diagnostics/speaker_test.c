/* SF32LB52-DevKit-LCD bounded speaker bring-up test.
 * Clock setup follows OpenSiFli SiFli-SDK drv_audcodec_m.c (Apache-2.0).
 * Direct HAL diagnostic only: not an openvela audio lower-half driver.
 */
#include <nuttx/config.h>
#include <nuttx/cache.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "bf0_hal.h"
#include "bf0_hal_audcodec.h"

static AUDCODEC_HandleTypeDef codec;
static DMA_HandleTypeDef dma;
static int32_t samples[480] __attribute__((aligned(32)));
static AUDCODE_DAC_CLK_CONFIG_TYPE clock_cfg =
  {48000, 0, 1, 0, 0x14d, 0, 5, 4, 2, 20, 20, 0};
extern void BSP_GPIO_Set(int pin, int val, int is_porta);

int main(int argc, char **argv)
{
  (void)argc; (void)argv;
  /* Channel 8 is not used by this board's active LCD/touch/UART setup.
   * Refuse to clobber an enabled channel. No NVIC interrupt is installed:
   * the fixed circular waveform needs no refill; stop after one second.
   */
  if (DMA1_Channel8->CCR & DMAC_CCR8_EN) {
    puts("speaker_test: DMA1 channel 8 busy; aborted"); return 1;
  }
  memset(&codec, 0, sizeof(codec)); memset(&dma, 0, sizeof(dma));
  for (int i=0; i<480; ++i) {
    int phase=i%48;
    int triangle=phase<24 ? phase-12 : 36-phase;
    samples[i]=triangle*32768; /* low-amplitude 1 kHz, signed 24-bit */
  }
  up_clean_dcache((uintptr_t)samples,(uintptr_t)(samples+480));
  BSP_GPIO_Set(10, 0, 1);
  HAL_PMU_EnableAudio(1);
  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC);
  HAL_TURN_ON_PLL(); /* analog reference enabled; DAC uses 48 MHz XTAL */
  codec.Instance=hwp_audcodec;
  codec.Init.dac_cfg.opmode=1;
  codec.Init.dac_cfg.dac_clk=&clock_cfg;
  dma.Instance=DMA1_Channel8; dma.Init.Request=41;
  codec.hdma[HAL_AUDCODEC_DAC_CH0]=&dma;
  int rc=HAL_AUDCODEC_Init(&codec);
  printf("speaker_test: init=%d\n",rc);
  if(rc!=HAL_OK) goto cleanup;
  rc=HAL_AUDCODEC_Config_TChanel(&codec,0,&codec.Init.dac_cfg);
  if(rc!=HAL_OK) goto cleanup;
  HAL_AUDCODEC_Config_DACPath_Volume(&codec,0,-18);
  rc=HAL_AUDCODEC_Transmit_DMA(&codec,(uint8_t *)samples,sizeof(samples),HAL_AUDCODEC_DAC_CH0);
  if(rc!=HAL_OK) goto cleanup;
  /* HAL starts DMA with refill interrupts. This fixed waveform has no ISR.
   * Disable those sources before enabling the DAC consumption clock. */
  __HAL_DMA_DISABLE_IT(&dma, DMA_IT_TC | DMA_IT_HT | DMA_IT_TE);
  __HAL_AUDCODEC_DAC_ENABLE(&codec);
  HAL_AUDCODEC_Config_DACPath(&codec,1);
  rc=HAL_AUDCODEC_Config_Analog_DACPath(&clock_cfg);
  if(rc!=HAL_OK) goto cleanup;
  HAL_AUDCODEC_Config_DACPath(&codec,0);
  BSP_GPIO_Set(10,1,1);
  printf("speaker_test: playing 1kHz for 1s, DMA remaining=%lu\n",(unsigned long)dma.Instance->CNDTR);
  fflush(stdout); usleep(1000000);
  printf("speaker_test: DMA remaining=%lu ISR=0x%lx\n",(unsigned long)dma.Instance->CNDTR,(unsigned long)hwp_dmac1->ISR);
cleanup:
  BSP_GPIO_Set(10,0,1);
  HAL_AUDCODEC_DMAStop(&codec,HAL_AUDCODEC_DAC_CH0);
  __HAL_AUDCODEC_DAC_DISABLE(&codec);
  HAL_AUDCODEC_Close_Analog_DACPath();
  HAL_AUDCODEC_DeInit(&codec);
  HAL_TURN_OFF_PLL();
  printf("speaker_test: stopped result=%d (audibility requires listener confirmation)\n",rc);
  return rc==HAL_OK?0:1;
}
