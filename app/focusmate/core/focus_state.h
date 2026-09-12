/****************************************************************************
 * app/focusmate/core/focus_state.h
 *
 * FocusMate state machine: states, events, and transition handling.
 * All external inputs (UI, timer, sensor, storage) are converted into
 * events and fed to focus_state_handle_event(); the state machine decides
 * transitions and what actions to run.
 ****************************************************************************/

#ifndef FOCUS_STATE_H
#define FOCUS_STATE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── FocusMate states ──────────────────────────────────────────── */

typedef enum {
  FM_IDLE,        /* waiting for goal input */
  FM_PLANNING,    /* AI is generating a structured plan */
  FM_READY,       /* plan ready, waiting for user to start */
  FM_FOCUSING,    /* actively focusing on current stage */
  FM_PAUSED,      /* user paused manually */
  FM_INTERRUPTED, /* phone removed / external interruption */
  FM_RECOVERING,  /* phone returned, prompting user to resume */
  FM_COMPLETED    /* all stages finished */
} fm_state_t;

/* ── Events ────────────────────────────────────────────────────── */

typedef enum {
  FM_EVT_NONE,
  FM_EVT_GOAL_SUBMITTED,  /* user entered goal + total time */
  FM_EVT_PLAN_READY,      /* AI/local planner produced a plan */
  FM_EVT_PLAN_FAILED,     /* planner could not produce a plan */
  FM_EVT_START,           /* user starts focusing */
  FM_EVT_PAUSE,           /* user pauses */
  FM_EVT_RESUME,          /* user resumes from pause */
  FM_EVT_PHONE_REMOVED,   /* sensor: phone taken away */
  FM_EVT_PHONE_RETURNED,  /* sensor: phone put back */
  FM_EVT_STAGE_TIMEOUT,   /* current stage finished */
  FM_EVT_SESSION_FINISHED,/* last stage finished */
  FM_EVT_RESTORE,         /* restore a saved session */
  FM_EVT_CANCEL           /* abandon current session */
} fm_event_t;

/* ── Plan / session data ───────────────────────────────────────── */

#define FM_MAX_STAGES 5
#define FM_MAX_TITLE_LEN 64
#define FM_MAX_GOAL_LEN 128

typedef struct {
  char title[FM_MAX_TITLE_LEN];
  int minutes;               /* planned minutes for this stage */
} fm_stage_t;

typedef struct {
  char goal[FM_MAX_GOAL_LEN];
  int total_minutes;
  int stage_count;
  int current_stage;         /* 0-based index */
  fm_stage_t stages[FM_MAX_STAGES];
  int elapsed_seconds;       /* focused seconds accumulated (all stages) */
  int stage_elapsed_seconds; /* seconds spent in the current stage */
  int interrupt_count;
  fm_state_t state;
} fm_session_t;

/* ── State machine API ─────────────────────────────────────────── */

/* Returns the human-readable name of a state. */
const char *fm_state_name(fm_state_t state);

/* Returns the human-readable name of an event. */
const char *fm_event_name(fm_event_t event);

/* Initialise the state machine with an empty session. */
void fm_state_init(fm_session_t *sess);

/* Set transition / enter-state hooks (called by fm_state_handle_event). */
typedef void (*fm_hook_fn)(fm_session_t *sess, fm_event_t evt);
void fm_state_set_hooks(fm_hook_fn on_transition, fm_hook_fn on_enter_state);

/*
 * Feed one event into the state machine.
 * May call back into ui/timer/storage/agent hooks (see fm_state_handlers).
 * Returns the new state.
 */
fm_state_t fm_state_handle_event(fm_session_t *sess, fm_event_t evt);

/* True if the given state is a "focusing-like" state (time runs). */
bool fm_state_is_active(fm_state_t state);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_STATE_H */
