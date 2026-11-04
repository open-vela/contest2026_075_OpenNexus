/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FOCUS_PHONE_GPIO_H
#define FOCUS_PHONE_GPIO_H
#include <stdbool.h>
int focus_phone_gpio_read(bool *present);
void focus_phone_gpio_close(void);
#endif
