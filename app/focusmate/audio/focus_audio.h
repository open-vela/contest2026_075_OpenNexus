/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FOCUS_AUDIO_H
#define FOCUS_AUDIO_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define FOCUS_AUDIO_COLUMNS 23
struct focus_audio_status {
  bool active, playing, ready, signal, limit;
  size_t bytes;
  unsigned milliseconds, peak, mean_abs;
  int error;
  uint8_t levels[FOCUS_AUDIO_COLUMNS];
};
int focus_audio_begin(void);
int focus_audio_end(bool keep);
int focus_audio_play(void);
void focus_audio_get(struct focus_audio_status *status);
void focus_audio_print(void);
int focus_audio_dump(void);
#endif
