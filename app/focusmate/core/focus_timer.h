/****************************************************************************
 * app/focusmate/core/focus_timer.h
 *
 * 1-second focus ticker. Only updates RAM/UI; storage writes happen on
 * key events (see focus_storage.h).
 ****************************************************************************/

#ifndef FOCUS_TIMER_H
#define FOCUS_TIMER_H

#include <stdbool.h>
#include <stdint.h>

#include "focus_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Start the 1s ticker. */
void focus_timer_start(void);

/* Stop the ticker. */
void focus_timer_stop(void);

/* True if the ticker is running. */
bool focus_timer_is_running(void);

/* Bind the session the ticker updates (call once at startup). */
void focus_timer_bind(fm_session_t *sess);

/* Register a per-second callback (UI refresh etc.). */
void focus_timer_set_tick_cb(void (*cb)(void));

/*
 * Called once per second while FOCUSING. Returns the number of
 * seconds elapsed in the current stage. When a stage's budget is
 * exhausted it emits FM_EVT_STAGE_TIMEOUT (or FM_EVT_SESSION_FINISHED
 * on the last stage) through fm_state_handle_event().
 */
void focus_timer_tick(fm_session_t *sess);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_TIMER_H */
