/* SPDX-License-Identifier: Apache-2.0
 * POSIX consumer of the board's read-only GPIO interface.
 */
#include <nuttx/config.h>
#include <nuttx/ioexpander/gpio.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include "phone_gpio.h"
#ifndef CONFIG_FOCUSMATE_PHONE_DEVPATH
#define CONFIG_FOCUSMATE_PHONE_DEVPATH "/dev/phone0"
#endif
static int phone_fd = -1;
static int last_level = -1;
static int last_error;
int focus_phone_gpio_read(bool *present)
{
  bool level = true;
  int error = 0;
  if (!present) return -EINVAL;
  if (phone_fd < 0) {
    phone_fd = open(CONFIG_FOCUSMATE_PHONE_DEVPATH, O_RDONLY);
    if (phone_fd < 0) error = errno;
  }
  if (!error && ioctl(phone_fd, GPIOC_READ, (unsigned long)&level) < 0) {
    error = errno;
    close(phone_fd); phone_fd = -1;
  }
  if (error) {
    if (error != last_error) {
      printf("[phone-gpio] device=%s error=%d state=unknown\n", CONFIG_FOCUSMATE_PHONE_DEVPATH,error);
      fflush(stdout);
    }
    last_error = error; last_level = -1;
    return -error;
  }
  *present = !level;
  if (last_error || last_level != (int)level) {
    printf("[phone-gpio] PA41 raw=%d present=%d\n", (int)level, (int)*present);
    fflush(stdout);
  }
  last_error = 0; last_level = (int)level;
  return 0;
}
void focus_phone_gpio_close(void)
{
  if (phone_fd >= 0) close(phone_fd);
  phone_fd = -1; last_level = -1; last_error = 0;
}
