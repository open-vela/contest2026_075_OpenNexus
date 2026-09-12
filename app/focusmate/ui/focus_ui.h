/****************************************************************************
 * app/focusmate/ui/focus_ui.h
 *
 * FocusMate LVGL UI: the display stack, the state-driven pages and the
 * on-screen controls.
 ****************************************************************************/

#ifndef FOCUS_UI_H
#define FOCUS_UI_H

#include "core/focus_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the display stack (lv_init + lv_nuttx_init on /dev/lcd0), build
 * the widget tree and start the LVGL refresh thread.  Returns 0 on success,
 * a negative errno when no display could be opened (the caller may then run
 * headless, CLI only).
 */

int focus_ui_init(void);

/* Refresh every widget from the current session.  Safe to call from any
 * thread; a no-op before focus_ui_init() succeeded.
 */

void focus_ui_refresh(const fm_session_t *sess);

/* Show a short status/notice message on the screen. */

void focus_ui_notice(const char *msg);

/* Stop the LVGL refresh thread.  The display keeps its last frame; the
 * widget tree is kept so a later focus_ui_init() can simply restart.
 */

void focus_ui_deinit(void);

/* ---------------------------------------------------------------------------
 * On-screen controls
 * ---------------------------------------------------------------------------
 * Touching a button does NOT touch the state machine.  The LVGL callback runs
 * on the refresh thread, deep inside lv_timer_handler(), while the state
 * machine, timer and storage are all owned by the CLI thread.  So a button
 * only records a request here, and the CLI thread pops it in its main loop
 * and applies it as a normal event.  That keeps every state transition on a
 * single thread and makes the button path and the CLI path share one code
 * path.
 */

typedef enum
{
  FM_UI_CMD_NONE = 0,
  FM_UI_CMD_START,
  FM_UI_CMD_PAUSE,
  FM_UI_CMD_RESUME,
  FM_UI_CMD_CANCEL,

  /* No goal has been set yet (IDLE after a cancel, or a fresh boot), so the
   * caller should plan a default session and start it straight away.  Without
   * this the key would go dead after a long press, which is exactly what the
   * first hardware test showed. */
  FM_UI_CMD_QUICKSTART
} fm_ui_cmd_t;

/* Pop the pending button request, or FM_UI_CMD_NONE when there is none.
 * Called from the CLI thread.
 */

fm_ui_cmd_t focus_ui_take_command(void);

/* True while the panel is up and accepting input. */

int focus_ui_has_touch(void);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_UI_H */
