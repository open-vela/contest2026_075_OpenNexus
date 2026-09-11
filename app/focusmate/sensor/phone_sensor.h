/****************************************************************************
 * app/focusmate/sensor/phone_sensor.h
 *
 * Phone presence sensor abstraction.
 *
 * Upper layers only depend on phone_sensor_get_state(). The Mock version
 * (default) and a real GPIO/ADC version can be swapped without touching
 * the state machine or UI.
 ****************************************************************************/

#ifndef PHONE_SENSOR_H
#define PHONE_SENSOR_H

#include <stdbool.h>

#include "core/focus_state.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  PHONE_UNKNOWN,
  PHONE_PRESENT,
  PHONE_ABSENT
} phone_state_t;

/* Read the current phone presence state. */
phone_state_t phone_sensor_get_state(void);

/* Initialise the sensor (mock by default). */
int phone_sensor_init(void);

/*
 * Mock helpers (M6): generate PHONE_REMOVED / PHONE_RETURNED events
 * through the state machine, simulating the sensor before real hardware.
 */
void phone_sensor_mock_removed(fm_session_t *sess);
void phone_sensor_mock_returned(fm_session_t *sess);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_SENSOR_H */
