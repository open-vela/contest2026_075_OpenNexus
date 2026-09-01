/****************************************************************************
 * app/focusmate/core/focus_timer.c
 *
 * FocusMate 1-second ticker (backed by a simple pthread + usleep loop;
 * can be swapped for a NuttX timer/wdog later).
 ****************************************************************************/

#include "focus_timer.h"

#include <pthread.h>
#include <unistd.h>

static bool s_running;
static pthread_t s_thread;
static bool s_thread_started;

static void *timer_loop(void *arg)
{
  (void)arg;
  while (s_running) {
    usleep(1000000); /* 1s */
    /* The actual per-second work is driven by the UI loop in M5;
     * this placeholder keeps the interface stable. */
  }
  return NULL;
}

void focus_timer_start(void)
{
  if (s_thread_started) {
    s_running = true;
    return;
  }
  s_running = true;
  pthread_create(&s_thread, NULL, timer_loop, NULL);
  s_thread_started = true;
}

void focus_timer_stop(void)
{
  s_running = false;
}

bool focus_timer_is_running(void)
{
  return s_running;
}

void focus_timer_tick(fm_session_t *sess)
{
  /* M5 will implement: decrement stage remaining, emit stage events. */
  (void)sess;
}
