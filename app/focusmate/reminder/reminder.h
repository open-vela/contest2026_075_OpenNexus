/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdint.h>
#define FM_REMINDER_QUEUE "/focusmate_reminder"
#define FM_REMINDER_TITLE 160
#define FM_REMINDER_EVENT_PATH "/data/focusmate/reminder-%s.json"
/* Fits the board's existing 32-byte POSIX message limit. Text is in a
 * uniquely named local file, not a pointer into another task's memory. */
struct fm_reminder_event { uint32_t version; char key[24]; };
int fm_prompt_read(const struct fm_reminder_event *,char *,unsigned);
