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

/* Create the LVGL UI (must be called after lv_init + display init). */
int focus_ui_init(void);

/* Refresh UI from the current session. */
void focus_ui_refresh(const fm_session_t *sess);

/* Show a short status/notice message on the screen. */
void focus_ui_notice(const char *msg);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_UI_H */
