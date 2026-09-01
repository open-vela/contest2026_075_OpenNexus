/****************************************************************************
 * app/focusmate/core/focus_state.c
 *
 * FocusMate state machine implementation.
 *
 * State flow:
 *   IDLE --GOAL_SUBMITTED--> PLANNING --PLAN_READY--> READY --START--> FOCUSING
 *   FOCUSING --PAUSE--> PAUSED --RESUME--> FOCUSING
 *   FOCUSING --PHONE_REMOVED--> INTERRUPTED --PHONE_RETURNED--> RECOVERING
 *   RECOVERING --RESUME--> FOCUSING
 *   FOCUSING --STAGE_TIMEOUT--> (next stage or COMPLETED)
 *   READY/FOCUSING/PAUSED/... --CANCEL--> IDLE
 ****************************************************************************/

#include "focus_state.h"

#include <string.h>

/* ── Event hooks (set by focusmate_main.c) ─────────────────────── */

static fm_hook_fn g_on_transition;
static fm_hook_fn g_on_enter_state;

void fm_state_set_hooks(fm_hook_fn on_transition, fm_hook_fn on_enter_state)
{
  g_on_transition = on_transition;
  g_on_enter_state = on_enter_state;
}

const char *fm_state_name(fm_state_t state)
{
  switch (state) {
  case FM_IDLE:        return "IDLE";
  case FM_PLANNING:    return "PLANNING";
  case FM_READY:       return "READY";
  case FM_FOCUSING:    return "FOCUSING";
  case FM_PAUSED:      return "PAUSED";
  case FM_INTERRUPTED: return "INTERRUPTED";
  case FM_RECOVERING:  return "RECOVERING";
  case FM_COMPLETED:   return "COMPLETED";
  default:             return "UNKNOWN";
  }
}

const char *fm_event_name(fm_event_t evt)
{
  switch (evt) {
  case FM_EVT_NONE:            return "NONE";
  case FM_EVT_GOAL_SUBMITTED:  return "GOAL_SUBMITTED";
  case FM_EVT_PLAN_READY:      return "PLAN_READY";
  case FM_EVT_PLAN_FAILED:     return "PLAN_FAILED";
  case FM_EVT_START:           return "START";
  case FM_EVT_PAUSE:           return "PAUSE";
  case FM_EVT_RESUME:          return "RESUME";
  case FM_EVT_PHONE_REMOVED:   return "PHONE_REMOVED";
  case FM_EVT_PHONE_RETURNED:  return "PHONE_RETURNED";
  case FM_EVT_STAGE_TIMEOUT:   return "STAGE_TIMEOUT";
  case FM_EVT_SESSION_FINISHED:return "SESSION_FINISHED";
  case FM_EVT_RESTORE:         return "RESTORE";
  case FM_EVT_CANCEL:          return "CANCEL";
  default:                     return "UNKNOWN";
  }
}

void fm_state_init(fm_session_t *sess)
{
  memset(sess, 0, sizeof(*sess));
  sess->state = FM_IDLE;
  sess->stage_count = 0;
  sess->current_stage = -1;
}

bool fm_state_is_active(fm_state_t state)
{
  return state == FM_FOCUSING;
}

/* Advance to the next stage, or finish the session. */
static fm_state_t advance_stage(fm_session_t *sess)
{
  if (sess->current_stage + 1 < sess->stage_count) {
    sess->current_stage++;
    return FM_FOCUSING;
  }
  return FM_COMPLETED;
}

fm_state_t fm_state_handle_event(fm_session_t *sess, fm_event_t evt)
{
  fm_state_t old = sess->state;
  fm_state_t next = old;

  switch (old) {
  case FM_IDLE:
    if (evt == FM_EVT_GOAL_SUBMITTED) {
      next = FM_PLANNING;
    } else if (evt == FM_EVT_RESTORE) {
      /* Restored session resumes from wherever it was saved. */
      if (sess->stage_count > 0) {
        next = FM_RECOVERING;
      }
    }
    break;

  case FM_PLANNING:
    if (evt == FM_EVT_PLAN_READY) {
      next = FM_READY;
    } else if (evt == FM_EVT_PLAN_FAILED) {
      next = FM_IDLE;
    } else if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_READY:
    if (evt == FM_EVT_START) {
      if (sess->current_stage < 0) {
        sess->current_stage = 0;
      }
      next = FM_FOCUSING;
    } else if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_FOCUSING:
    if (evt == FM_EVT_PAUSE) {
      next = FM_PAUSED;
    } else if (evt == FM_EVT_PHONE_REMOVED) {
      sess->interrupt_count++;
      next = FM_INTERRUPTED;
    } else if (evt == FM_EVT_STAGE_TIMEOUT) {
      next = advance_stage(sess);
    } else if (evt == FM_EVT_SESSION_FINISHED) {
      next = FM_COMPLETED;
    } else if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_PAUSED:
    if (evt == FM_EVT_RESUME) {
      next = FM_FOCUSING;
    } else if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_INTERRUPTED:
    if (evt == FM_EVT_PHONE_RETURNED) {
      next = FM_RECOVERING;
    } else if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_RECOVERING:
    if (evt == FM_EVT_RESUME) {
      next = FM_FOCUSING;
    } else if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_COMPLETED:
    if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  default:
    break;
  }

  sess->state = next;

  if (g_on_transition) {
    g_on_transition(sess, evt);
  }
  if (next != old && g_on_enter_state) {
    g_on_enter_state(sess, evt);
  }

  return next;
}
