/****************************************************************************
 * app/focusmate/ui/focus_ui.h
 *
 * FocusMate LVGL UI: three pages - idle (goal input), focusing
 * (stage/timer/progress), completed (summary).
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

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_UI_H */
