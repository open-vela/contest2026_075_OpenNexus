/****************************************************************************
 * app/focusmate/storage/focus_storage.h
 *
 * Session persistence (NOR/flash). Saves/loads a JSON session file.
 * Path is configurable via CONFIG_FOCUSMATE_DATA_DIR.
 ****************************************************************************/

#ifndef FOCUS_STORAGE_H
#define FOCUS_STORAGE_H

#include "core/focus_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise storage (create data dir if needed). Returns 0 on success. */
int focus_storage_init(void);

/* Save the current session to disk. Returns 0 on success. */
int focus_storage_save(const fm_session_t *sess);

/* Load a session from disk. Returns 0 if a session was restored. */
int focus_storage_load(fm_session_t *sess);

/* Remove the saved session. */
int focus_storage_clear(void);

/* Append a completed session to the history file. Returns 0 on success. */
int focus_storage_append_history(const fm_session_t *sess);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_STORAGE_H */
