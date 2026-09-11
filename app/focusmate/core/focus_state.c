/****************************************************************************
 * app/focusmate/core/focus_state.c
 ****************************************************************************/

#include "focus_state.h"

#include <string.h>

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
  case FM_IDLE:           return "IDLE";
  case FM_PLANNING:       return "PLANNING";
  case FM_READY:          return "READY";
  case FM_FOCUSING:       return "FOCUSING";
  case FM_PAUSED:         return "PAUSED";
  case FM_INTERRUPTED:    return "INTERRUPTED";
  case FM_RECOVERING:     return "RECOVERING";
  case FM_REVIEWING:      return "REVIEWING";
  case FM_SETTLE_CONFIRM: return "SETTLE_CONFIRM";
  case FM_COMPLETED:      return "COMPLETED";
  default:                return "UNKNOWN";
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
  case FM_EVT_ROUND_END:       return "ROUND_END";
  case FM_EVT_STAGE_TIMEOUT:   return "STAGE_TIMEOUT";
  case FM_EVT_SESSION_FINISHED:return "SESSION_FINISHED";
  case FM_EVT_REVIEW_NONE:     return "REVIEW_NONE";
  case FM_EVT_REVIEW_DONE:     return "REVIEW_DONE";
  case FM_EVT_SETTLE_REQUEST:  return "SETTLE_REQUEST";
  case FM_EVT_SETTLE_CANCEL:   return "SETTLE_CANCEL";
  case FM_EVT_SETTLE_CONFIRM:  return "SETTLE_CONFIRM";
  case FM_EVT_RESTORE:         return "RESTORE";
  case FM_EVT_CANCEL:          return "CANCEL";
  default:                     return "UNKNOWN";
  }
}

void fm_state_init(fm_session_t *sess)
{
  memset(sess, 0, sizeof(*sess));
  sess->state = FM_IDLE;
  sess->state_before_settle = FM_IDLE;
  sess->stage_count = 0;
  sess->current_stage = -1;
}

bool fm_state_is_active(fm_state_t state)
{
  return state == FM_FOCUSING;
}

static int next_uncompleted_stage(const fm_session_t *sess)
{
  int i;

  for (i = 0; i < sess->stage_count && i < FM_MAX_STAGES; i++) {
    if (!sess->stages[i].completed) {
      return i;
    }
  }
  return -1;
}

static fm_state_t finish_review(fm_session_t *sess, fm_event_t evt)
{
  int idx;
  int next;

  if (evt == FM_EVT_REVIEW_DONE &&
      sess->last_round_completed >= 1 &&
      sess->last_round_completed <= sess->stage_count) {
    idx = sess->last_round_completed - 1;
    if (!sess->stages[idx].completed) {
      sess->stages[idx].completed = true;
      sess->completed_task_count++;
    }
  }

  sess->last_round_completed = 0;

  if (sess->settlement_pending) {
    sess->settlement_pending = false;
    return FM_COMPLETED;
  }

  next = next_uncompleted_stage(sess);
  if (next < 0) {
    return FM_COMPLETED;
  }

  sess->current_stage = next;
  sess->stage_elapsed_seconds = 0;
  return FM_READY;
}

fm_state_t fm_state_handle_event(fm_session_t *sess, fm_event_t evt)
{
  fm_state_t old = sess->state;
  fm_state_t next = old;

  switch (old) {
  case FM_IDLE:
    if (evt == FM_EVT_GOAL_SUBMITTED) {
      next = FM_PLANNING;
    } else if (evt == FM_EVT_RESTORE && sess->stage_count > 0) {
      next = FM_RECOVERING;
    }
    break;

  case FM_PLANNING:
    if (evt == FM_EVT_PLAN_READY) {
      next = FM_READY;
    } else if (evt == FM_EVT_PLAN_FAILED || evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_READY:
    if (evt == FM_EVT_START) {
      int idx = next_uncompleted_stage(sess);
      if (idx < 0) {
        next = FM_COMPLETED;
      } else {
        sess->current_stage = idx;
        sess->stage_elapsed_seconds = 0;
        sess->round_count++;
        next = FM_FOCUSING;
      }
    } else if (evt == FM_EVT_SETTLE_REQUEST) {
      sess->state_before_settle = old;
      next = FM_SETTLE_CONFIRM;
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
    } else if (evt == FM_EVT_ROUND_END ||
               evt == FM_EVT_STAGE_TIMEOUT ||
               evt == FM_EVT_SESSION_FINISHED) {
      next = FM_REVIEWING;
    } else if (evt == FM_EVT_SETTLE_REQUEST) {
      sess->state_before_settle = old;
      next = FM_SETTLE_CONFIRM;
    } else if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_PAUSED:
    if (evt == FM_EVT_RESUME) {
      next = FM_FOCUSING;
    } else if (evt == FM_EVT_ROUND_END) {
      next = FM_REVIEWING;
    } else if (evt == FM_EVT_SETTLE_REQUEST) {
      sess->state_before_settle = old;
      next = FM_SETTLE_CONFIRM;
    } else if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_INTERRUPTED:
    if (evt == FM_EVT_PHONE_RETURNED) {
      next = FM_RECOVERING;
    } else if (evt == FM_EVT_SETTLE_REQUEST) {
      sess->state_before_settle = old;
      next = FM_SETTLE_CONFIRM;
    } else if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_RECOVERING:
    if (evt == FM_EVT_RESUME) {
      next = FM_FOCUSING;
    } else if (evt == FM_EVT_SETTLE_REQUEST) {
      sess->state_before_settle = old;
      next = FM_SETTLE_CONFIRM;
    } else if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_REVIEWING:
    if (evt == FM_EVT_REVIEW_NONE || evt == FM_EVT_REVIEW_DONE) {
      next = finish_review(sess, evt);
    } else if (evt == FM_EVT_CANCEL) {
      next = FM_IDLE;
    }
    break;

  case FM_SETTLE_CONFIRM:
    if (evt == FM_EVT_SETTLE_CANCEL) {
      next = sess->state_before_settle;
    } else if (evt == FM_EVT_SETTLE_CONFIRM) {
      sess->settlement_pending = true;
      sess->early_exit = true;
      next = FM_REVIEWING;
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
