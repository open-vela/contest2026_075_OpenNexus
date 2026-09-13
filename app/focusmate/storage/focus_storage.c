/****************************************************************************
 * app/focusmate/storage/focus_storage.c
 *
 * Session persistence (M8): JSON session file via cJSON.
 *
 * - focusmate/session.json : current (possibly unfinished) session
 * - focusmate/history.json : completed session records (kept across restarts)
 ****************************************************************************/

#include "focus_storage.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <netutils/cJSON.h>

#ifndef CONFIG_FOCUSMATE_DATA_DIR
#define CONFIG_FOCUSMATE_DATA_DIR "/data/focusmate"
#endif

#define FOCUS_DATA_DIR CONFIG_FOCUSMATE_DATA_DIR
#define FOCUS_SESSION_FILE FOCUS_DATA_DIR "/session.json"
#define FOCUS_HISTORY_FILE FOCUS_DATA_DIR "/history.json"
#define FOCUS_LIBRARY_FILE FOCUS_DATA_DIR "/library.json"

static cJSON *stages_to_json(const fm_session_t *sess)
{
  cJSON *arr = cJSON_CreateArray();
  int i;
  if (!arr) {
    return NULL;
  }
  for (i = 0; i < sess->stage_count && i < FM_MAX_STAGES; i++) {
    cJSON *st = cJSON_CreateObject();
    if (!st) {
      continue;
    }
    cJSON_AddStringToObject(st, "title", sess->stages[i].title);
    cJSON_AddNumberToObject(st, "minutes", sess->stages[i].minutes);
    cJSON_AddBoolToObject(st, "completed", sess->stages[i].completed);
    cJSON_AddItemToArray(arr, st);
  }
  return arr;
}

static int json_to_session(const cJSON *root, fm_session_t *sess)
{
  cJSON *goal, *total, *sc, *cur, *el, *sel, *ic, *rc, *cc, *ee, *keep, *arr, *st;
  int i, n;

  if (!root) {
    return -1;
  }
  goal = cJSON_GetObjectItem(root, "goal");
  total = cJSON_GetObjectItem(root, "total_minutes");
  sc = cJSON_GetObjectItem(root, "stage_count");
  cur = cJSON_GetObjectItem(root, "current_stage");
  el = cJSON_GetObjectItem(root, "elapsed_seconds");
  sel = cJSON_GetObjectItem(root, "stage_elapsed_seconds");
  ic = cJSON_GetObjectItem(root, "interrupt_count");
  rc = cJSON_GetObjectItem(root, "round_count");
  cc = cJSON_GetObjectItem(root, "completed_task_count");
  ee = cJSON_GetObjectItem(root, "early_exit");
  keep = cJSON_GetObjectItem(root, "keep_for_resume");
  arr = cJSON_GetObjectItem(root, "stages");

  if (!cJSON_IsString(goal) || !cJSON_IsNumber(sc)) {
    return -1;
  }
  n = (int)sc->valuedouble;
  if (n < 0 || n > FM_MAX_STAGES) {
    return -1;
  }

  memset(sess, 0, sizeof(*sess));
  strncpy(sess->goal, goal->valuestring, sizeof(sess->goal) - 1);
  sess->goal[sizeof(sess->goal) - 1] = '\0';
  sess->stage_count = n;
  sess->total_minutes = cJSON_IsNumber(total) ? (int)total->valuedouble : 0;
  sess->current_stage = cJSON_IsNumber(cur) ? (int)cur->valuedouble : -1;
  sess->elapsed_seconds = cJSON_IsNumber(el) ? (int)el->valuedouble : 0;
  sess->stage_elapsed_seconds =
      cJSON_IsNumber(sel) ? (int)sel->valuedouble : 0;
  sess->interrupt_count = cJSON_IsNumber(ic) ? (int)ic->valuedouble : 0;
  sess->round_count = cJSON_IsNumber(rc) ? (int)rc->valuedouble : 0;
  sess->completed_task_count = cJSON_IsNumber(cc) ? (int)cc->valuedouble : 0;
  sess->early_exit = cJSON_IsBool(ee) ? cJSON_IsTrue(ee) : false;
  sess->keep_for_resume =
      cJSON_IsBool(keep) ? cJSON_IsTrue(keep) : false;

  if (cJSON_IsArray(arr)) {
    for (i = 0; i < n && i < FM_MAX_STAGES; i++) {
      st = cJSON_GetArrayItem(arr, i);
      if (!st) {
        break;
      }
      cJSON *title = cJSON_GetObjectItem(st, "title");
      cJSON *minutes = cJSON_GetObjectItem(st, "minutes");
      cJSON *completed = cJSON_GetObjectItem(st, "completed");
      if (cJSON_IsString(title)) {
        strncpy(sess->stages[i].title, title->valuestring,
                sizeof(sess->stages[i].title) - 1);
        sess->stages[i].title[sizeof(sess->stages[i].title) - 1] = '\0';
      }
      if (cJSON_IsNumber(minutes)) {
        sess->stages[i].minutes = (int)minutes->valuedouble;
      }
      if (cJSON_IsBool(completed)) {
        sess->stages[i].completed = cJSON_IsTrue(completed);
      }
    }
  }
  return 0;
}

int focus_storage_init(void)
{
  mkdir(FOCUS_DATA_DIR, 0777);
  return 0;
}

int focus_storage_save(const fm_session_t *sess)
{
  cJSON *root;
  char *json_str;
  FILE *fp;
  int rc = -1;

  if (!sess) {
    return -1;
  }

  root = cJSON_CreateObject();
  if (!root) {
    return -1;
  }
  cJSON_AddStringToObject(root, "goal", sess->goal);
  cJSON_AddNumberToObject(root, "total_minutes", sess->total_minutes);
  cJSON_AddNumberToObject(root, "stage_count", sess->stage_count);
  cJSON_AddNumberToObject(root, "current_stage", sess->current_stage);
  cJSON_AddNumberToObject(root, "elapsed_seconds", sess->elapsed_seconds);
  cJSON_AddNumberToObject(root, "stage_elapsed_seconds",
                          sess->stage_elapsed_seconds);
  cJSON_AddNumberToObject(root, "interrupt_count", sess->interrupt_count);
  cJSON_AddNumberToObject(root, "round_count", sess->round_count);
  cJSON_AddNumberToObject(root, "completed_task_count",
                          sess->completed_task_count);
  cJSON_AddBoolToObject(root, "early_exit", sess->early_exit);
  cJSON_AddBoolToObject(root, "keep_for_resume", sess->keep_for_resume);
  cJSON_AddItemToObject(root, "stages", stages_to_json(sess));

  json_str = cJSON_PrintUnformatted(root);
  if (!json_str) {
    cJSON_Delete(root);
    return -1;
  }

  fp = fopen(FOCUS_SESSION_FILE, "w");
  if (fp) {
    fputs(json_str, fp);
    fclose(fp);
    rc = 0;
  }
  free(json_str);
  cJSON_Delete(root);
  return rc;
}

int focus_storage_load(fm_session_t *sess)
{
  FILE *fp;
  char *buf;
  long len;
  cJSON *root;
  int rc;

  fp = fopen(FOCUS_SESSION_FILE, "rb");
  if (!fp) {
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (len <= 0 || len > 65536) {
    fclose(fp);
    return -1;
  }
  buf = malloc((size_t)len + 1);
  if (!buf) {
    fclose(fp);
    return -1;
  }
  if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
    free(buf);
    fclose(fp);
    return -1;
  }
  buf[len] = '\0';
  fclose(fp);

  root = cJSON_Parse(buf);
  free(buf);
  rc = json_to_session(root, sess);
  if (root) {
    cJSON_Delete(root);
  }
  return rc;
}

int focus_storage_load_last_unfinished(fm_session_t *sess)
{
  FILE *fp;
  char *buf;
  long len;
  cJSON *root;
  cJSON *hist;
  int i;
  int rc = -1;

  if (!sess) {
    return -1;
  }

  /* Prefer the current resumable session when it still has open tasks. */
  if (focus_storage_load(sess) == 0 &&
      sess->stage_count > 0 &&
      sess->completed_task_count < sess->stage_count) {
    for (i = 0; i < sess->stage_count && i < FM_MAX_STAGES; i++) {
      if (!sess->stages[i].completed) {
        sess->current_stage = i;
        break;
      }
    }
    return 0;
  }

  fp = fopen(FOCUS_HISTORY_FILE, "rb");
  if (!fp) {
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (len <= 0 || len > 65536) {
    fclose(fp);
    return -1;
  }
  buf = malloc((size_t)len + 1);
  if (!buf) {
    fclose(fp);
    return -1;
  }
  if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
    free(buf);
    fclose(fp);
    return -1;
  }
  buf[len] = '\0';
  fclose(fp);

  root = cJSON_Parse(buf);
  free(buf);
  if (!root) {
    return -1;
  }

  hist = cJSON_GetObjectItem(root, "history");
  if (cJSON_IsArray(hist)) {
    for (i = cJSON_GetArraySize(hist) - 1; i >= 0; i--) {
      cJSON *entry = cJSON_GetArrayItem(hist, i);
      cJSON *cc = cJSON_GetObjectItem(entry, "completed_task_count");
      cJSON *sc = cJSON_GetObjectItem(entry, "stage_count");
      if (!cJSON_IsNumber(cc) || !cJSON_IsNumber(sc) ||
          cc->valueint >= sc->valueint) {
        continue;
      }
      if (json_to_session(entry, sess) == 0) {
        for (int j = 0; j < sess->stage_count && j < FM_MAX_STAGES; j++) {
          if (!sess->stages[j].completed) {
            sess->current_stage = j;
            break;
          }
        }
        rc = 0;
        break;
      }
    }
  }

  cJSON_Delete(root);
  return rc;
}

int focus_storage_clear(void)
{
  remove(FOCUS_SESSION_FILE);
  return 0;
}

/* ── Completed-session history (M8) ────────────────────────────── */

int focus_storage_append_history(const fm_session_t *sess)
{
  cJSON *root;
  cJSON *hist;
  char *json_str;
  FILE *fp;
  long len;
  int rc = 0;

  root = cJSON_CreateObject();
  if (!root) {
    return -1;
  }

  /* Load existing history if present. */
  fp = fopen(FOCUS_HISTORY_FILE, "rb");
  if (fp) {
    char *buf;
    fseek(fp, 0, SEEK_END);
    len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len > 0 && len <= 65536) {
      buf = malloc((size_t)len + 1);
      if (buf) {
        if (fread(buf, 1, (size_t)len, fp) == (size_t)len) {
          buf[len] = '\0';
          cJSON *old = cJSON_Parse(buf);
          if (old) {
            cJSON_Delete(root);
            root = old;
          }
        }
        free(buf);
      }
    }
    fclose(fp);
  }

  hist = cJSON_GetObjectItem(root, "history");
  if (!cJSON_IsArray(hist)) {
    hist = cJSON_AddArrayToObject(root, "history");
  }

  cJSON *entry = cJSON_CreateObject();
  if (entry) {
    cJSON_AddStringToObject(entry, "goal", sess->goal);
    cJSON_AddNumberToObject(entry, "total_minutes", sess->total_minutes);
    cJSON_AddNumberToObject(entry, "elapsed_seconds", sess->elapsed_seconds);
    cJSON_AddNumberToObject(entry, "interrupt_count",
                            sess->interrupt_count);
    cJSON_AddNumberToObject(entry, "stage_count", sess->stage_count);
    cJSON_AddNumberToObject(entry, "round_count", sess->round_count);
    cJSON_AddNumberToObject(entry, "completed_task_count",
                            sess->completed_task_count);
    cJSON_AddBoolToObject(entry, "early_exit", sess->early_exit);
    cJSON_AddBoolToObject(entry, "keep_for_resume", sess->keep_for_resume);
    cJSON_AddItemToObject(entry, "stages", stages_to_json(sess));
    cJSON_AddItemToArray(hist, entry);
  }

  json_str = cJSON_PrintUnformatted(root);
  if (json_str) {
    fp = fopen(FOCUS_HISTORY_FILE, "w");
    if (fp) {
      fputs(json_str, fp);
      fclose(fp);
    } else {
      rc = -1;
    }
    free(json_str);
  } else {
    rc = -1;
  }
  cJSON_Delete(root);
  return rc;
}


/* ── Unfinished task library (maximum 4 entries) ───────────────── */

static cJSON *json_file_load(const char *path)
{
  FILE *fp;
  char *buf;
  long len;
  cJSON *root;

  fp = fopen(path, "rb");
  if (!fp) {
    return NULL;
  }
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (len <= 0 || len > 262144) {
    fclose(fp);
    return NULL;
  }
  buf = malloc((size_t)len + 1);
  if (!buf) {
    fclose(fp);
    return NULL;
  }
  if (fread(buf, 1, (size_t)len, fp) != (size_t)len) {
    free(buf);
    fclose(fp);
    return NULL;
  }
  buf[len] = '\0';
  fclose(fp);
  root = cJSON_Parse(buf);
  free(buf);
  return root;
}

static int json_file_write(const char *path, const cJSON *root)
{
  char *json;
  FILE *fp;
  int rc = -1;

  json = cJSON_PrintUnformatted(root);
  if (!json) {
    return -1;
  }
  fp = fopen(path, "w");
  if (fp) {
    fputs(json, fp);
    fclose(fp);
    rc = 0;
  }
  free(json);
  return rc;
}

static cJSON *library_tasks(cJSON **root_out)
{
  cJSON *root = json_file_load(FOCUS_LIBRARY_FILE);
  cJSON *tasks;

  if (!root) {
    root = cJSON_CreateObject();
  }
  if (!root) {
    return NULL;
  }
  tasks = cJSON_GetObjectItem(root, "tasks");
  if (!cJSON_IsArray(tasks)) {
    tasks = cJSON_AddArrayToObject(root, "tasks");
  }
  *root_out = root;
  return tasks;
}

static int library_entry_unfinished(const cJSON *entry)
{
  cJSON *cc = cJSON_GetObjectItem(entry, "completed_task_count");
  cJSON *sc = cJSON_GetObjectItem(entry, "stage_count");

  return cJSON_IsNumber(cc) && cJSON_IsNumber(sc) &&
         cc->valueint < sc->valueint;
}

static cJSON *session_to_library_entry(const fm_session_t *sess)
{
  cJSON *entry = cJSON_CreateObject();

  if (!entry) {
    return NULL;
  }
  cJSON_AddStringToObject(entry, "goal", sess->goal);
  cJSON_AddNumberToObject(entry, "total_minutes", sess->total_minutes);
  cJSON_AddNumberToObject(entry, "stage_count", sess->stage_count);
  cJSON_AddNumberToObject(entry, "current_stage", sess->current_stage);
  cJSON_AddNumberToObject(entry, "elapsed_seconds", sess->elapsed_seconds);
  cJSON_AddNumberToObject(entry, "stage_elapsed_seconds",
                          sess->stage_elapsed_seconds);
  cJSON_AddNumberToObject(entry, "interrupt_count", sess->interrupt_count);
  cJSON_AddNumberToObject(entry, "round_count", sess->round_count);
  cJSON_AddNumberToObject(entry, "completed_task_count",
                          sess->completed_task_count);
  cJSON_AddBoolToObject(entry, "early_exit", sess->early_exit);
  cJSON_AddBoolToObject(entry, "keep_for_resume", true);
  cJSON_AddItemToObject(entry, "stages", stages_to_json(sess));
  return entry;
}

int focus_storage_save_to_library(const fm_session_t *sess)
{
  cJSON *root;
  cJSON *tasks;
  cJSON *entry;
  cJSON *item;
  cJSON *goal;
  int i;
  int rc;

  if (!sess || sess->stage_count <= 0) {
    return -1;
  }

  tasks = library_tasks(&root);
  if (!tasks) {
    return -1;
  }

  /* Update by goal; a completed task is removed instead of stored. */
  for (i = cJSON_GetArraySize(tasks) - 1; i >= 0; i--) {
    item = cJSON_GetArrayItem(tasks, i);
    goal = cJSON_GetObjectItem(item, "goal");
    if (cJSON_IsString(goal) &&
        strcmp(goal->valuestring, sess->goal) == 0) {
      cJSON_DeleteItemFromArray(tasks, i);
    }
  }

  if (sess->completed_task_count < sess->stage_count) {
    while (cJSON_GetArraySize(tasks) >= FOCUS_LIBRARY_MAX) {
      cJSON_DeleteItemFromArray(tasks, 0);
    }
    entry = session_to_library_entry(sess);
    if (!entry) {
      cJSON_Delete(root);
      return -1;
    }
    cJSON_AddItemToArray(tasks, entry);
  }

  rc = json_file_write(FOCUS_LIBRARY_FILE, root);
  cJSON_Delete(root);
  return rc;
}

int focus_storage_library_count(void)
{
  cJSON *root = NULL;
  cJSON *tasks = library_tasks(&root);
  int count = 0;
  int i;

  if (!tasks) {
    return 0;
  }
  for (i = 0; i < cJSON_GetArraySize(tasks); i++) {
    if (library_entry_unfinished(cJSON_GetArrayItem(tasks, i))) {
      count++;
    }
  }
  cJSON_Delete(root);
  return count;
}

int focus_storage_load_library(int index, fm_session_t *sess)
{
  cJSON *root = NULL;
  cJSON *tasks = library_tasks(&root);
  cJSON *entry;
  int seen = 0;
  int i;

  if (!tasks || !sess || index < 0) {
    if (root) {
      cJSON_Delete(root);
    }
    return -1;
  }

  for (i = 0; i < cJSON_GetArraySize(tasks); i++) {
    entry = cJSON_GetArrayItem(tasks, i);
    if (!library_entry_unfinished(entry)) {
      continue;
    }
    if (seen++ == index) {
      if (json_to_session(entry, sess) == 0) {
        if (sess->current_stage < 0 ||
            sess->stages[sess->current_stage].completed) {
          for (int j = 0; j < sess->stage_count && j < FM_MAX_STAGES; j++) {
            if (!sess->stages[j].completed) {
              sess->current_stage = j;
              break;
            }
          }
        }
        cJSON_Delete(root);
        return 0;
      }
      break;
    }
  }

  cJSON_Delete(root);
  return -1;
}

int focus_storage_library_title(int index, char *out, int out_size)
{
  cJSON *root = NULL;
  cJSON *tasks = library_tasks(&root);
  cJSON *entry;
  cJSON *goal;
  int seen = 0;
  int i;

  if (!tasks || !out || out_size <= 0 || index < 0) {
    if (root) {
      cJSON_Delete(root);
    }
    return -1;
  }

  for (i = 0; i < cJSON_GetArraySize(tasks); i++) {
    entry = cJSON_GetArrayItem(tasks, i);
    if (!library_entry_unfinished(entry)) {
      continue;
    }
    if (seen++ == index) {
      goal = cJSON_GetObjectItem(entry, "goal");
      if (!cJSON_IsString(goal)) {
        break;
      }
      strncpy(out, goal->valuestring, (size_t)out_size - 1);
      out[out_size - 1] = '\0';
      cJSON_Delete(root);
      return 0;
    }
  }

  cJSON_Delete(root);
  return -1;
}
