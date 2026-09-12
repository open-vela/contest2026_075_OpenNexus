/****************************************************************************
 * app/focusmate/core/focus_timer.c
 *
 * FocusMate 1-second focus ticker (M5).
 *
 * A dedicated pthread ticks once per second while FOCUSING. Each tick
 * increments the current stage's elapsed seconds; when a stage budget is
 * exhausted it emits FM_EVT_STAGE_TIMEOUT (or FM_EVT_SESSION_FINISHED on
 * the last stage) through fm_state_handle_event().
 *
 * Only RAM/UI is updated here; storage writes happen on state transitions
 * via the hooks in focusmate_main.c.
 ****************************************************************************/

#include "focus_timer.h"

#include <pthread.h>
#include <unistd.h>

static bool s_running;
static pthread_t s_thread;
static bool s_thread_started;
static fm_session_t *s_sess;
static void (*s_tick_cb)(void);

static void *timer_loop(void *arg)
{
  (void)arg;
  while (s_running) {
    usleep(1000000); /* 1s */
    if (!s_running || !s_sess) {
      continue;
    }
    focus_timer_tick(s_sess);
    if (s_tick_cb) {
      s_tick_cb();
    }
  }
  return NULL;
}

void focus_timer_start(void)
{
  if (s_running) {
    return; /* already ticking */
  }
  s_running = true;
  if (s_thread_started) {
    /* Previous thread exited (stop was called); spawn a fresh one. */
    pthread_create(&s_thread, NULL, timer_loop, NULL);
    return;
  }
  pthread_create(&s_thread, NULL, timer_loop, NULL);
  s_thread_started = true;
}

void focus_timer_stop(void)
{
  s_running = false;
  /* Give the ticker thread a moment to observe the flag and exit;
   * it will be re-created on the next focus_timer_start(). */
  usleep(20000);
}

bool focus_timer_is_running(void)
{
  return s_running;
}

/* Bind the session the ticker updates (call once at startup). */
void focus_timer_bind(fm_session_t *sess)
{
  s_sess = sess;
}

/* Register a per-second callback (UI refresh etc.). */
void focus_timer_set_tick_cb(void (*cb)(void))
{
  s_tick_cb = cb;
}

void focus_timer_tick(fm_session_t *sess)
{
  int stage_idx;
  int round_total_sec;

  if (!sess || sess->state != FM_FOCUSING) {
    return; /* timer only runs while focusing */
  }

  stage_idx = sess->current_stage;
  if (stage_idx < 0 || stage_idx >= sess->stage_count) {
    /* No valid stage: finish the session. */
    fm_state_handle_event(sess, FM_EVT_SESSION_FINISHED);
    return;
  }

  sess->elapsed_seconds++;
  sess->stage_elapsed_seconds++;

  round_total_sec = sess->total_minutes * 60;
  if (round_total_sec > 0 &&
      sess->stage_elapsed_seconds >= round_total_sec) {
    fm_state_handle_event(sess, FM_EVT_STAGE_TIMEOUT);
  }
}
