/****************************************************************************
 * app/focusmate/focusmate_main.c
 *
 * FocusMate - AI 桌面专注伙伴 (M3 skeleton)
 *
 * A minimal NSH application that:
 *   1. initialises the state machine / storage / sensor / agent / UI
 *   2. runs an interactive command loop for manual testing
 *   3. demonstrates the full state machine via CLI commands
 *
 * M4+ replaces the CLI-driven plan with the real focus-planner skill.
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "core/focus_state.h"
#include "core/focus_timer.h"
#include "agent/focus_agent.h"
#include "sensor/phone_sensor.h"
#include "storage/focus_storage.h"
#include "ui/focus_ui.h"

static fm_session_t g_session;

static void on_transition(fm_session_t *sess, fm_event_t evt)
{
  (void)evt;
  /* Persist on meaningful transitions (M6 refines checkpoints). */
  focus_storage_save(sess);
}

static void on_enter_state(fm_session_t *sess, fm_event_t evt)
{
  (void)evt;
  printf("[FocusMate] state -> %s\n", fm_state_name(sess->state));

  if (sess->state == FM_FOCUSING) {
    focus_timer_start();
  } else if (sess->state == FM_PAUSED ||
             sess->state == FM_INTERRUPTED ||
             sess->state == FM_COMPLETED) {
    focus_timer_stop();
  }

  if (sess->state == FM_COMPLETED) {
    char summary[256];
    focus_agent_summarize(sess, summary, sizeof(summary));
    printf("[FocusMate] %s\n", summary);
  }
}

static void print_usage(void)
{
  printf(
    "FocusMate commands:\n"
    "  goal <text> <minutes>   submit goal + total time (-> PLANNING)\n"
    "  plan_ready              simulate AI plan ready (-> READY)\n"
    "  start                   start focusing\n"
    "  pause / resume          pause / resume\n"
    "  removed / returned      mock phone removed / returned\n"
    "  stage_done              simulate current stage finished\n"
    "  cancel                  abandon session\n"
    "  status                  print current state\n"
    "  save / restore          save / load session\n"
    "  help                    this message\n"
    "  quit                    exit FocusMate\n");
}

static void cmd_goal(const char *text, int minutes)
{
  if (minutes <= 0) {
    minutes = 45;
  }
  /* Let the agent (or local fallback) build the plan immediately. */
  focus_agent_plan(&g_session, text, minutes);
  fm_state_handle_event(&g_session, FM_EVT_GOAL_SUBMITTED);
  fm_state_handle_event(&g_session, FM_EVT_PLAN_READY);
}

int main(int argc, char *argv[])
{
  printf("FocusMate starting (M3 skeleton)\n");

  focus_storage_init();
  phone_sensor_init();
  focus_agent_init();

  fm_state_set_hooks(on_transition, on_enter_state);
  fm_state_init(&g_session);

  /* Try to restore an unfinished session. */
  if (focus_storage_load(&g_session) == 0 && g_session.stage_count > 0) {
    printf("[FocusMate] restored session: %s\n",
           g_session.goal[0] ? g_session.goal : "(no goal)");
    fm_state_handle_event(&g_session, FM_EVT_RESTORE);
  } else {
    fm_state_init(&g_session);
  }

  printf("[FocusMate] state -> %s\n", fm_state_name(g_session.state));
  print_usage();

  char line[256];
  while (1) {
    printf("focusmate> ");
    fflush(stdout);
    if (!fgets(line, sizeof(line), stdin)) {
      break;
    }
    line[strcspn(line, "\r\n")] = '\0';
    if (line[0] == '\0') {
      continue;
    }

    char *cmd = strtok(line, " ");
    if (!cmd) {
      continue;
    }

    if (strcmp(cmd, "quit") == 0 || strcmp(cmd, "exit") == 0) {
      break;
    } else if (strcmp(cmd, "help") == 0) {
      print_usage();
    } else if (strcmp(cmd, "status") == 0) {
      printf("state=%s goal='%s' stage=%d/%d elapsed=%d interrupts=%d\n",
             fm_state_name(g_session.state), g_session.goal,
             g_session.current_stage + 1, g_session.stage_count,
             g_session.elapsed_seconds, g_session.interrupt_count);
    } else if (strcmp(cmd, "goal") == 0) {
      char *text = strtok(NULL, " ");
      char *minstr = strtok(NULL, " ");
      if (text) {
        cmd_goal(text, minstr ? atoi(minstr) : 45);
      }
    } else if (strcmp(cmd, "plan_ready") == 0) {
      fm_state_handle_event(&g_session, FM_EVT_PLAN_READY);
    } else if (strcmp(cmd, "start") == 0) {
      fm_state_handle_event(&g_session, FM_EVT_START);
    } else if (strcmp(cmd, "pause") == 0) {
      fm_state_handle_event(&g_session, FM_EVT_PAUSE);
    } else if (strcmp(cmd, "resume") == 0) {
      fm_state_handle_event(&g_session, FM_EVT_RESUME);
    } else if (strcmp(cmd, "removed") == 0) {
      phone_sensor_mock_removed(&g_session);
    } else if (strcmp(cmd, "returned") == 0) {
      phone_sensor_mock_returned(&g_session);
    } else if (strcmp(cmd, "stage_done") == 0) {
      fm_state_handle_event(&g_session, FM_EVT_STAGE_TIMEOUT);
    } else if (strcmp(cmd, "cancel") == 0) {
      fm_state_handle_event(&g_session, FM_EVT_CANCEL);
      focus_storage_clear();
    } else if (strcmp(cmd, "save") == 0) {
      focus_storage_save(&g_session);
      printf("[FocusMate] session saved\n");
    } else if (strcmp(cmd, "restore") == 0) {
      if (focus_storage_load(&g_session) == 0 && g_session.stage_count > 0) {
        fm_state_handle_event(&g_session, FM_EVT_RESTORE);
      } else {
        printf("[FocusMate] nothing to restore\n");
      }
    } else {
      printf("Unknown command: %s (type 'help')\n", cmd);
    }
  }

  printf("FocusMate exiting\n");
  return 0;
}
