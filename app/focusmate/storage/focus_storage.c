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
  cJSON *goal, *total, *sc, *cur, *el, *sel, *ic, *rc, *cc, *ee, *arr, *st;
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
