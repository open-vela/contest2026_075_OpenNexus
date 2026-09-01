/****************************************************************************
 * app/focusmate/storage/focus_storage.c
 *
 * Session persistence backed by a simple key=value text file for the
 * skeleton (M8 will move to cJSON + NOR with full field fidelity).
 ****************************************************************************/

#include "focus_storage.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifndef CONFIG_FOCUSMATE_DATA_DIR
#define CONFIG_FOCUSMATE_DATA_DIR "/data/focusmate"
#endif

#define FOCUS_SESSION_FILE CONFIG_FOCUSMATE_DATA_DIR "/session.dat"

int focus_storage_init(void)
{
  mkdir(CONFIG_FOCUSMATE_DATA_DIR, 0777);
  return 0;
}

int focus_storage_save(const fm_session_t *sess)
{
  FILE *fp = fopen(FOCUS_SESSION_FILE, "w");
  if (!fp) {
    return -1;
  }
  fprintf(fp, "state=%d\n", (int)sess->state);
  fprintf(fp, "goal=%s\n", sess->goal);
  fprintf(fp, "total_minutes=%d\n", sess->total_minutes);
  fprintf(fp, "stage_count=%d\n", sess->stage_count);
  fprintf(fp, "current_stage=%d\n", sess->current_stage);
  fprintf(fp, "elapsed_seconds=%d\n", sess->elapsed_seconds);
  fprintf(fp, "stage_elapsed_seconds=%d\n", sess->stage_elapsed_seconds);
  fprintf(fp, "interrupt_count=%d\n", sess->interrupt_count);
  for (int i = 0; i < sess->stage_count && i < FM_MAX_STAGES; i++) {
    fprintf(fp, "stage_%d_title=%s\n", i, sess->stages[i].title);
    fprintf(fp, "stage_%d_minutes=%d\n", i, sess->stages[i].minutes);
  }
  fclose(fp);
  return 0;
}

int focus_storage_load(fm_session_t *sess)
{
  FILE *fp = fopen(FOCUS_SESSION_FILE, "r");
  char line[256];
  if (!fp) {
    return -1;
  }
  memset(sess, 0, sizeof(*sess));
  while (fgets(line, sizeof(line), fp)) {
    char *eq = strchr(line, '=');
    if (!eq) {
      continue;
    }
    *eq = '\0';
    const char *key = line;
    char *val = eq + 1;
    val[strcspn(val, "\r\n")] = '\0';

    if (strcmp(key, "state") == 0) {
      sess->state = (fm_state_t)atoi(val);
    } else if (strcmp(key, "goal") == 0) {
      strncpy(sess->goal, val, sizeof(sess->goal) - 1);
    } else if (strcmp(key, "total_minutes") == 0) {
      sess->total_minutes = atoi(val);
    } else if (strcmp(key, "stage_count") == 0) {
      sess->stage_count = atoi(val);
    } else if (strcmp(key, "current_stage") == 0) {
      sess->current_stage = atoi(val);
    } else if (strcmp(key, "elapsed_seconds") == 0) {
      sess->elapsed_seconds = atoi(val);
    } else if (strcmp(key, "stage_elapsed_seconds") == 0) {
      sess->stage_elapsed_seconds = atoi(val);
    } else if (strcmp(key, "interrupt_count") == 0) {
      sess->interrupt_count = atoi(val);
    } else if (strncmp(key, "stage_", 6) == 0) {
      int idx = atoi(key + 6);
      const char *field = strchr(key + 6, '_');
      if (field && idx >= 0 && idx < FM_MAX_STAGES) {
        if (strcmp(field, "_title") == 0) {
          strncpy(sess->stages[idx].title, val,
                  sizeof(sess->stages[idx].title) - 1);
        } else if (strcmp(field, "_minutes") == 0) {
          sess->stages[idx].minutes = atoi(val);
        }
      }
    }
  }
  fclose(fp);
  return 0;
}

int focus_storage_clear(void)
{
  remove(FOCUS_SESSION_FILE);
  return 0;
}
