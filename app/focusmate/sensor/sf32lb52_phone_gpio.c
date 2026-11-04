/* SPDX-License-Identifier: Apache-2.0
 * SF32LB52-DevKit-LCD: PA41 on header pin 5, GND on pin 6.
 * Read-only openvela GPIO lower half. Never configures an output.
 */
#include <nuttx/config.h>
#include <nuttx/ioexpander/gpio.h>
#include <errno.h>
#include <stdio.h>
#include "bf0_hal.h"
#include "sfconfig.h"
#if !defined(CONFIG_BSP_LCDC_USING_QADSPI) || defined(BSP_LCDC_USING_DBI)
#error "PA41 phone input verified only with this board's QSPI display configuration"
#endif
static void phone_pin_input(void)
{
  GPIO_InitTypeDef pin = {0};
  HAL_PIN_Set(PAD_PA41, GPIO_A41, PIN_PULLUP, 1);
  pin.Pin = 41;
  pin.Mode = GPIO_MODE_INPUT;
  pin.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(hwp_gpio1, &pin);
}
static int phone_read(struct gpio_dev_s *dev, bool *value)
{
  (void)dev;
  *value = HAL_GPIO_ReadPin(hwp_gpio1, 41) != GPIO_PIN_RESET;
  return 0;
}
static int phone_settype(struct gpio_dev_s *dev, enum gpio_pintype_e type)
{
  if (type != GPIO_INPUT_PIN_PULLUP) return -EPERM;
  phone_pin_input();
  dev->gp_pintype = GPIO_INPUT_PIN_PULLUP;
  return 0;
}
static const struct gpio_operations_s phone_ops = {
  .go_read = phone_read,
  .go_setpintype = phone_settype,
};
static struct gpio_dev_s phone_dev = {
  .gp_pintype = GPIO_INPUT_PIN_PULLUP,
  .gp_ops = &phone_ops,
};
int focus_phone_gpio_initialize(void)
{
  static bool registered;
  if (registered) return 0;
  phone_pin_input();
  int ret = gpio_pin_register_byname(&phone_dev, "phone0");
  if (ret == 0) registered = true;
  printf("[phone-board] PA41 input-pullup /dev/phone0 register=%d raw=%d\n",
         ret, HAL_GPIO_ReadPin(hwp_gpio1, 41) != GPIO_PIN_RESET);
  return ret;
}
