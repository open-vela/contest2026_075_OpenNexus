/* SPDX-License-Identifier: Apache-2.0 */
#ifndef FOCUSMATE_BLE_FRAME_H
#define FOCUSMATE_BLE_FRAME_H
#include <stdbool.h>
#include <stddef.h>
#define FM_BLE_LINE_MAX 1024
struct fm_ble_frame { char line[FM_BLE_LINE_MAX]; size_t used; bool dropping; };
typedef int (*fm_ble_line_cb)(const char *, size_t, void *);
void fm_ble_frame_reset(struct fm_ble_frame *);
int fm_ble_frame_feed(struct fm_ble_frame *, const void *, size_t,
                      fm_ble_line_cb, void *);
#endif
