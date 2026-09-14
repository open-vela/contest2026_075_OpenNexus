/****************************************************************************
 * app/focusmate/storage/focus_storage.h
 *
 * Session persistence (NOR/flash). Saves/loads a JSON session file.
 * Path is configurable via CONFIG_FOCUSMATE_DATA_DIR.
 ****************************************************************************/

#ifndef FOCUS_STORAGE_H
#define FOCUS_STORAGE_H

#include "core/focus_state.h"

#define FOCUS_LIBRARY_MAX 4

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise storage (create data dir if needed). Returns 0 on success. */
int focus_storage_init(void);

/* Save the current session to disk. Returns 0 on success. */
int focus_storage_save(const fm_session_t *sess);

/* Load a session from disk. Returns 0 if a session was restored. */
int focus_storage_load(fm_session_t *sess);

/* Load the latest unfinished task from the history library. */
int focus_storage_load_last_unfinished(fm_session_t *sess);

/* Save an unfinished task into the task library (maximum 4 entries). */
int focus_storage_save_to_library(const fm_session_t *sess);

/* Return the number of unfinished tasks currently in the library. */
int focus_storage_library_count(void);

/* Load unfinished task by index, newest entries last. */
int focus_storage_load_library(int index, fm_session_t *sess);

/* Copy the goal of unfinished task index into out. */
int focus_storage_library_title(int index, char *out, int out_size);

/* Delete unfinished task by index (newest entries last). */
int focus_storage_delete_library(int index);

/* Remove the saved session. */
int focus_storage_clear(void);

/* Append a completed session to the history file. Returns 0 on success. */
int focus_storage_append_history(const fm_session_t *sess);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_STORAGE_H */
