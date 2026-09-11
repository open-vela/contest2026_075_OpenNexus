/****************************************************************************
 * app/focusmate/sensor/phone_sensor.c
 *
 * Mock phone presence sensor (M6 placeholder).
 * Real GPIO/ADC implementation will replace this file later (M7).
 ****************************************************************************/

#include "phone_sensor.h"

static phone_state_t s_state = PHONE_PRESENT;

int phone_sensor_init(void)
{
  s_state = PHONE_PRESENT;
  return 0;
}

phone_state_t phone_sensor_get_state(void)
{
  return s_state;
}

void phone_sensor_mock_removed(fm_session_t *sess)
{
  s_state = PHONE_ABSENT;
  fm_state_handle_event(sess, FM_EVT_PHONE_REMOVED);
}

void phone_sensor_mock_returned(fm_session_t *sess)
{
  s_state = PHONE_PRESENT;
  fm_state_handle_event(sess, FM_EVT_PHONE_RETURNED);
}
