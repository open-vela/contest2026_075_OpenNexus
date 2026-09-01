/****************************************************************************
 * app/focusmate/agent/focus_agent.c
 *
 * Placeholder agent bridge for M3 skeleton.
 * M4 replaces focus_agent_plan() with a real focus-planner skill call
 * through the velaclaw client, plus a local fallback planner.
 ****************************************************************************/

#include "focus_agent.h"

#include <string.h>

static bool s_connected;

int focus_agent_init(void)
{
  s_connected = false; /* velaclaw client_open comes in M4 */
  return 0;
}

bool focus_agent_is_connected(void)
{
  return s_connected;
}

/* Local fallback: split the goal into 3 conservative stages. */
static int local_default_plan(fm_session_t *sess, const char *goal,
                              int total_minutes)
{
  int n = total_minutes < 30 ? 2 : 3;
  if (n > FM_MAX_STAGES) {
    n = FM_MAX_STAGES;
  }
  sess->stage_count = n;
  sess->total_minutes = total_minutes;
  strncpy(sess->goal, goal, sizeof(sess->goal) - 1);
  sess->goal[sizeof(sess->goal) - 1] = '\0';

  const char *default_titles[FM_MAX_STAGES] = {
    "整理需求与思路",
    "执行主要工作",
    "检查与收尾"
  };
  int base = total_minutes / n;
  int rem = total_minutes % n;
  for (int i = 0; i < n; i++) {
    strncpy(sess->stages[i].title, default_titles[i],
            sizeof(sess->stages[i].title) - 1);
    sess->stages[i].minutes = base + (i < rem ? 1 : 0);
  }
  return 0;
}

int focus_agent_plan(fm_session_t *sess, const char *goal, int total_minutes)
{
  /* M4: try AI planner first, fall back to local_default_plan on failure. */
  return local_default_plan(sess, goal, total_minutes);
}

int focus_agent_summarize(const fm_session_t *sess, char *out, int out_size)
{
  (void)sess;
  if (out && out_size > 0) {
    strncpy(out, "专注完成，继续保持！", out_size - 1);
    out[out_size - 1] = '\0';
  }
  return 0;
}
