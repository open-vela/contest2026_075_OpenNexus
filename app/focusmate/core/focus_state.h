/****************************************************************************
 * app/focusmate/core/focus_state.h
 *
 * FocusMate state machine: states, events, and transition handling.
 ****************************************************************************/

#ifndef FOCUS_STATE_H
#define FOCUS_STATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  FM_IDLE,
  FM_PLANNING,
  FM_READY,
  FM_FOCUSING,
  FM_PAUSED,
  FM_INTERRUPTED,
  FM_RECOVERING,
  FM_REVIEWING,
  FM_SETTLE_CONFIRM,
  FM_COMPLETED
} fm_state_t;

typedef enum {
  FM_EVT_NONE,
  FM_EVT_GOAL_SUBMITTED,
  FM_EVT_PLAN_READY,
  FM_EVT_PLAN_FAILED,
  FM_EVT_START,
  FM_EVT_PAUSE,
  FM_EVT_RESUME,
  FM_EVT_PHONE_REMOVED,
  FM_EVT_PHONE_RETURNED,
  FM_EVT_ROUND_END,
  FM_EVT_STAGE_TIMEOUT,
  FM_EVT_SESSION_FINISHED,
  FM_EVT_REVIEW_NONE,
  FM_EVT_REVIEW_DONE,
  FM_EVT_SETTLE_REQUEST,
  FM_EVT_SETTLE_CANCEL,
  FM_EVT_SETTLE_CONFIRM,
  FM_EVT_RESTORE,
  FM_EVT_CANCEL
} fm_event_t;

#define FM_MAX_STAGES 4
#define FM_MAX_TITLE_LEN 64
#define FM_MAX_GOAL_LEN 128

typedef struct {
  char title[FM_MAX_TITLE_LEN];
  int minutes;
  bool completed;
} fm_stage_t;

typedef struct {
  char goal[FM_MAX_GOAL_LEN];
  int total_minutes;
  int stage_count;
  int current_stage;
  fm_stage_t stages[FM_MAX_STAGES];
  int elapsed_seconds;
  int stage_elapsed_seconds;
  int interrupt_count;
  int round_count;
  int completed_task_count;
  int last_round_completed; /* 0 = none, otherwise stage index + 1 */
  bool settlement_pending;
  bool early_exit;
  fm_state_t state_before_settle;
  fm_state_t state;
} fm_session_t;

const char *fm_state_name(fm_state_t state);
const char *fm_event_name(fm_event_t event);
void fm_state_init(fm_session_t *sess);

typedef void (*fm_hook_fn)(fm_session_t *sess, fm_event_t evt);
void fm_state_set_hooks(fm_hook_fn on_transition, fm_hook_fn on_enter_state);

fm_state_t fm_state_handle_event(fm_session_t *sess, fm_event_t evt);
bool fm_state_is_active(fm_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_STATE_H */
