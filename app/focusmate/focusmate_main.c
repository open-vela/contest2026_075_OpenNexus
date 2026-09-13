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
#include <unistd.h>
#include <sys/select.h>

#include "core/focus_state.h"
#include "core/focus_timer.h"
#include "agent/focus_agent.h"
#include "sensor/phone_sensor.h"
#include "storage/focus_storage.h"
#include "ui/focus_ui.h"

static fm_session_t g_session;
static int g_task_serial;

static void next_task_name(char *out, int out_size)
{
  int n = g_task_serial++;

  if (n < 26) {
    snprintf(out, (size_t)out_size, "任务%c", 'A' + n);
  } else {
    snprintf(out, (size_t)out_size, "任务%c%c",
             'A' + (n / 26 - 1), 'A' + (n % 26));
  }
}

static void on_transition(fm_session_t *sess, fm_event_t evt)
{
  (void)evt;
  /* Persist on meaningful transitions (M6 refines checkpoints). */
  focus_storage_save(sess);
}

static void on_enter_state(fm_session_t *sess, fm_event_t evt)
{
  (void)evt;
  printf("\n[FocusMate] state -> %s\n", fm_state_name(sess->state));

  if (sess->state == FM_FOCUSING) {
    focus_timer_start();
  } else if (sess->state == FM_PAUSED ||
             sess->state == FM_INTERRUPTED ||
             sess->state == FM_REVIEWING ||
             sess->state == FM_SETTLE_CONFIRM ||
             sess->state == FM_COMPLETED) {
    focus_timer_stop();
  }

  switch (sess->state) {
  case FM_INTERRUPTED:
    /* Active scene: the device sensed the phone was taken away and
     * auto-paused — the user said nothing. */
    if (sess->current_stage >= 0 && sess->current_stage < sess->stage_count) {
      printf("[FocusMate] !!! 检测到手机被取走，专注已自动暂停 !!!\n");
      printf("[FocusMate]     当前任务: %s (第 %d/%d 阶段)\n",
             sess->stages[sess->current_stage].title,
             sess->current_stage + 1, sess->stage_count);
      printf("[FocusMate]     已保存进度，中断次数 +1 (%d)\n",
             sess->interrupt_count);
    }
    break;

  case FM_RECOVERING:
    /* Active scene: phone put back — proactively ask to resume. */
    if (sess->current_stage >= 0 && sess->current_stage < sess->stage_count) {
      printf("[FocusMate] 欢迎回来！手机已放回。\n");
      printf("[FocusMate]     刚才正在: %s (第 %d/%d 阶段)\n",
             sess->stages[sess->current_stage].title,
             sess->current_stage + 1, sess->stage_count);
      printf("[FocusMate]     是否继续？输入 resume 继续，cancel 放弃。\n");
    }
    break;

  case FM_COMPLETED:
    printf("[FocusMate] 完成 %d/%d\n",
           sess->completed_task_count, sess->stage_count);
    printf("[FocusMate] 专注 %d 分钟 | 中断 %d 次\n",
           sess->elapsed_seconds / 60, sess->interrupt_count);
    focus_storage_append_history(sess);
    focus_storage_save_to_library(sess);
    if (sess->keep_for_resume) {
      focus_storage_save(sess);
      printf("[FocusMate] 任务已保留到后台任务库，可稍后恢复。\n");
    } else {
      focus_storage_clear();
    }
    break;

  default:
    break;
  }

  /* Keep the on-screen UI in step with every state change. */
  focus_ui_refresh(sess);
}

/* Called once per second by the timer while FOCUSING (M5).
 * Uses \r so the countdown overwrites itself on the same line,
 * keeping the CLI readable between commands. */
static void on_tick(void)
{
  int stage_total, remain, pct;

  if (g_session.state != FM_FOCUSING) {
    return;
  }
  if (g_session.current_stage >= 0 &&
      g_session.current_stage < g_session.stage_count) {
    stage_total = g_session.total_minutes * 60;
    remain = stage_total - g_session.stage_elapsed_seconds;
    pct = stage_total > 0
              ? (g_session.stage_elapsed_seconds * 100) / stage_total
              : 0;
    if (remain < 0) {
      remain = 0;
    }
    if (pct > 100) {
      pct = 100;
    }
    printf("\r[FocusMate] [%d/%d] %s  剩余 %02d:%02d  进度 %d%%   ",
           g_session.current_stage + 1, g_session.stage_count,
           g_session.stages[g_session.current_stage].title,
           remain / 60, remain % 60, pct);
  }
  fflush(stdout);

  /* Push the same countdown onto the AMOLED once per second. */
  focus_ui_refresh(&g_session);
}

/* Name a screen-originated command so the console log shows what was tapped. */

static const char *ui_cmd_name(fm_ui_cmd_t c)
{
  switch (c) {
  case FM_UI_CMD_START:
    return "START";
  case FM_UI_CMD_TASK_SELECTED:
    return "TASK_SELECTED";
  case FM_UI_CMD_DURATION_SELECTED:
    return "DURATION_SELECTED";
  case FM_UI_CMD_DURATION_CANCEL:
    return "DURATION_CANCEL";
  case FM_UI_CMD_PAUSE:
    return "PAUSE";
  case FM_UI_CMD_RESUME:
    return "RESUME";
  case FM_UI_CMD_CANCEL:
    return "STOP";
  case FM_UI_CMD_QUICKSTART:
    return "QUICKSTART";
  case FM_UI_CMD_LIBRARY:
    return "LIBRARY";
  case FM_UI_CMD_LIBRARY_ENTER:
    return "LIBRARY_ENTER";
  case FM_UI_CMD_LIBRARY_BACK:
    return "LIBRARY_BACK";
  case FM_UI_CMD_HOME:
    return "HOME";
  case FM_UI_CMD_END_ROUND:
    return "END_ROUND";
  case FM_UI_CMD_ROUND_CONTINUE:
    return "ROUND_CONTINUE";
  case FM_UI_CMD_ROUND_ABANDON:
    return "ROUND_ABANDON";
  case FM_UI_CMD_EXIT:
    return "EXIT_REQUEST";
  case FM_UI_CMD_EXIT_CONFIRM:
    return "EXIT_CONFIRM";
  case FM_UI_CMD_EXIT_CANCEL:
    return "EXIT_CANCEL";
  case FM_UI_CMD_REVIEW_YES:
    return "REVIEW_YES";
  case FM_UI_CMD_REVIEW_NONE:
    return "REVIEW_NONE";
  case FM_UI_CMD_SETTLE_REQUEST:
    return "SETTLE_REQUEST";
  case FM_UI_CMD_SETTLE_CANCEL:
    return "SETTLE_CANCEL";
  case FM_UI_CMD_SETTLE_CONFIRM:
    return "SETTLE_CONFIRM";
  default:
    return "?";
  }
}

static void print_usage(void)
{
  printf(
    "FocusMate commands:\n"
    "  goal <text>             submit goal (-> PLANNING)\n"
    "  plan_ready              simulate AI plan ready (-> READY)\n"
    "  start                   open task selection\n"
    "  task <1..4>             choose task for this round -> duration\n"
    "  duration <25|30|45|60>  select this round duration and start\n"
    "  pause / resume          pause / resume\n"
    "  removed / returned      mock phone removed / returned\n"
    "  round_end / stage_done  end current round manually -> REVIEWING\n"
    "  timeout                 simulate focus-round timeout -> REVIEWING\n"
    "  review_none             task not completed this round\n"
    "  review_done             task completed this round\n"
    "  round_abandon           abandon current round without recording\n"
    "  library                 open latest unfinished task library item\n"
    "  end_task                request full-task exit\n"
    "  exit_yes / exit_no      confirm or cancel full-task exit\n"
    "  settle                  request early settlement (-> SETTLE_CONFIRM)\n"
    "  settle_yes / settle_no  confirm or cancel settlement\n"
    "  cancel                  abandon session\n"
    "  status                  print current state\n"
    "  agent                   show ai_agent connection status\n"
    "  demo                    run core active-scene demo\n"
    "  save / restore          save / load session\n"
    "  help                    this message\n"
    "  quit                    exit FocusMate\n");
}

/* Forward declarations */
static void cmd_goal(const char *text, int minutes);
static void cmd_demo(void);

static void cmd_demo(void)
{
  printf("\n===== FocusMate Demo v2 =====\n");
  if (g_session.state != FM_READY) {
    if (g_session.state != FM_IDLE) {
      fm_state_handle_event(&g_session, FM_EVT_CANCEL);
    }
    cmd_goal("完成比赛演示", 0);
  }

  fm_state_handle_event(&g_session, FM_EVT_START);
  g_session.current_stage = 0;
  fm_state_handle_event(&g_session, FM_EVT_TASK_SELECTED);
  g_session.total_minutes = 25;
  fm_state_handle_event(&g_session, FM_EVT_DURATION_SELECTED);
  sleep(2);
  phone_sensor_mock_removed(&g_session);
  sleep(1);
  phone_sensor_mock_returned(&g_session);
  sleep(1);
  fm_state_handle_event(&g_session, FM_EVT_RESUME);
  sleep(1);
  fm_state_handle_event(&g_session, FM_EVT_ROUND_END);
  sleep(1);
  fm_state_handle_event(&g_session, FM_EVT_REVIEW_DONE);
  printf("===== Demo v2 end =====\n");
}

static void cmd_goal(const char *text, int minutes)
{
  (void)minutes;

  fm_state_init(&g_session);
  fm_state_handle_event(&g_session, FM_EVT_GOAL_SUBMITTED);
  usleep(700000);

  printf("[FocusMate] planning goal: %s...\n", text);
  if (focus_agent_plan(&g_session, text) == 0 &&
      g_session.stage_count > 0) {
    printf("[FocusMate] plan ready: %d tasks\n", g_session.stage_count);
    for (int i = 0; i < g_session.stage_count; i++) {
      printf("  [%d/%d] %s\n", i + 1, g_session.stage_count,
             g_session.stages[i].title);
    }
    fm_state_handle_event(&g_session, FM_EVT_PLAN_READY);
  } else {
    fm_state_handle_event(&g_session, FM_EVT_PLAN_FAILED);
  }
}

/* Apply an input command (screen button or board key) to the session.
 * Kept in one place so the interactive CLI loop and the autostart loop cannot
 * drift apart. */

static void apply_ui_command(fm_ui_cmd_t ucmd)
{
  printf("\n[FocusMate] input: %s\n", ui_cmd_name(ucmd));
  fflush(stdout);

  switch (ucmd) {
  case FM_UI_CMD_START:
    fm_state_handle_event(&g_session, FM_EVT_START);
    break;
  case FM_UI_CMD_DURATION_SELECTED: {
    int minutes = focus_ui_take_duration();
    if (minutes > 0) {
      g_session.total_minutes = minutes;
      fm_state_handle_event(&g_session, FM_EVT_DURATION_SELECTED);
    }
    break;
  }
  case FM_UI_CMD_DURATION_CANCEL:
    fm_state_handle_event(&g_session, FM_EVT_CANCEL);
    break;
  case FM_UI_CMD_PAUSE:
    fm_state_handle_event(&g_session, FM_EVT_PAUSE);
    break;
  case FM_UI_CMD_RESUME:
    fm_state_handle_event(&g_session, FM_EVT_RESUME);
    break;
  case FM_UI_CMD_CANCEL:
    fm_state_handle_event(&g_session, FM_EVT_CANCEL);
    focus_storage_clear();
    break;
  case FM_UI_CMD_QUICKSTART: {
    char task_name[32];

    /* HOLD on the home screen always starts a new task.  Continuing an
     * unfinished task is an explicit LIBRARY action instead. */
    if (g_session.stage_count > 0 &&
        g_session.completed_task_count < g_session.stage_count) {
      focus_storage_save_to_library(&g_session);
    }
    next_task_name(task_name, sizeof(task_name));
    cmd_goal(task_name, 0);
    break;
  }
  case FM_UI_CMD_HOME:
    fm_state_handle_event(&g_session, FM_EVT_HOME);
    break;
  case FM_UI_CMD_LIBRARY:
    fm_state_handle_event(&g_session, FM_EVT_LIBRARY_OPEN);
    break;
  case FM_UI_CMD_LIBRARY_ENTER: {
    int selected = focus_ui_take_library_selection();
    if (selected >= 0 &&
        focus_storage_load_library(selected, &g_session) == 0) {
      fm_state_handle_event(&g_session, FM_EVT_RESTORE);
    } else {
      focus_ui_notice("请先选择一个待完成任务");
    }
    break;
  }
  case FM_UI_CMD_LIBRARY_BACK:
    fm_state_handle_event(&g_session, FM_EVT_HOME);
    break;
  case FM_UI_CMD_END_ROUND:
    fm_state_handle_event(&g_session, FM_EVT_ROUND_END);
    break;
  case FM_UI_CMD_ROUND_CONTINUE:
    fm_state_handle_event(&g_session, FM_EVT_ROUND_CONTINUE);
    break;
  case FM_UI_CMD_ROUND_ABANDON:
    fm_state_handle_event(&g_session, FM_EVT_ROUND_ABANDON);
    break;
  case FM_UI_CMD_EXIT:
    fm_state_handle_event(&g_session, FM_EVT_EXIT_REQUEST);
    break;
  case FM_UI_CMD_EXIT_CONFIRM:
    fm_state_handle_event(&g_session, FM_EVT_EXIT_CONFIRM);
    break;
  case FM_UI_CMD_EXIT_CANCEL:
    fm_state_handle_event(&g_session, FM_EVT_EXIT_CANCEL);
    break;
  case FM_UI_CMD_TASK_SELECTED: {
    int selected = focus_ui_take_review_selection();
    if (selected >= 1 && selected <= g_session.stage_count &&
        !g_session.stages[selected - 1].completed) {
      g_session.current_stage = selected - 1;
      fm_state_handle_event(&g_session, FM_EVT_TASK_SELECTED);
    }
    break;
  }
  case FM_UI_CMD_REVIEW_YES:
    fm_state_handle_event(&g_session, FM_EVT_REVIEW_DONE);
    break;
  case FM_UI_CMD_REVIEW_NONE:
    fm_state_handle_event(&g_session, FM_EVT_REVIEW_NONE);
    break;
  case FM_UI_CMD_SETTLE_REQUEST:
    fm_state_handle_event(&g_session, FM_EVT_SETTLE_REQUEST);
    break;
  case FM_UI_CMD_SETTLE_CANCEL:
    fm_state_handle_event(&g_session, FM_EVT_SETTLE_CANCEL);
    break;
  case FM_UI_CMD_SETTLE_CONFIRM:
    fm_state_handle_event(&g_session, FM_EVT_SETTLE_CONFIRM);
    break;
  default:
    break;
  }
}

int main(int argc, char *argv[])
{
  int no_cli = 0;
  int i;

  /* Started from /etc/init.d/rcS at boot: keep driving the panel and the key,
   * but leave stdin alone so NSH on the console stays usable. */
  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--no-cli") == 0) {
      no_cli = 1;
    }
  }

  printf("FocusMate starting%s\n", no_cli ? " (autostart)" : "");

  focus_storage_init();
  phone_sensor_init();
  focus_agent_init();

  /* Bring up the AMOLED UI.  A missing display only downgrades FocusMate
   * to the CLI; it never aborts the session. */
  if (focus_ui_init() < 0) {
    printf("[FocusMate] 显示屏不可用，仅使用命令行界面\n");
  }

  fm_state_set_hooks(on_transition, on_enter_state);
  fm_state_init(&g_session);
  focus_timer_bind(&g_session);
  focus_timer_set_tick_cb(on_tick);

  /* The start screen owns recovery now: do not jump directly into an old
   * session, but tell the user when the task library has one available. */
  fm_state_init(&g_session);
  {
    fm_session_t saved;
    if (focus_storage_load_last_unfinished(&saved) == 0 &&
        saved.stage_count > 0) {
      printf("\n[FocusMate] 任务仓库中检测到未完成任务：%s\n",
             saved.goal[0] ? saved.goal : "(无目标)");
      printf("[FocusMate] 点击 LIBRARY 或输入 library 恢复。\n");
    }
  }

  printf("[FocusMate] state -> %s\n", fm_state_name(g_session.state));
  focus_ui_refresh(&g_session);

  if (no_cli) {
    /* Autostart: drive the panel and the board key only, leaving stdin to
     * NSH so the serial console stays usable for debugging. */
    printf("[FocusMate] autostart: use the board key (KEY2).\n");
    while (1) {
      fm_ui_cmd_t ucmd = focus_ui_take_command();

      if (ucmd != FM_UI_CMD_NONE) {
        apply_ui_command(ucmd);
      }
      usleep(100000);
    }
  }

  print_usage();

  char line[256];
  while (1) {
    fm_ui_cmd_t ucmd;

    /* A tap on the panel or a press of the board key.  The callback only
     * leaves a request behind; the state machine is driven from here so that
     * every transition and every storage write stays on this one thread. */
    ucmd = focus_ui_take_command();
    if (ucmd != FM_UI_CMD_NONE) {
      apply_ui_command(ucmd);
      continue;
    }

    /* Console input, polled with a short timeout so that taps are still acted
     * on promptly while the user types nothing. */
    {
      fd_set rfds;
      struct timeval tv;

      FD_ZERO(&rfds);
      FD_SET(0, &rfds);
      tv.tv_sec = 0;
      tv.tv_usec = 100000;

      if (select(1, &rfds, NULL, NULL, &tv) <= 0) {
        continue;
      }
    }

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
      printf("state=%s goal='%s' stage=%d/%d completed=%d/%d rounds=%d "
             "elapsed=%d stage_sec=%d interrupts=%d\n",
             fm_state_name(g_session.state), g_session.goal,
             g_session.current_stage + 1, g_session.stage_count,
             g_session.completed_task_count, g_session.stage_count,
             g_session.round_count, g_session.elapsed_seconds,
             g_session.stage_elapsed_seconds, g_session.interrupt_count);
    } else if (strcmp(cmd, "agent") == 0) {
      printf("ai_agent connected: %s\n",
             focus_agent_is_connected() ? "yes" : "no");
    } else if (strcmp(cmd, "demo") == 0) {
      cmd_demo();
    } else if (strcmp(cmd, "goal") == 0) {
      char *text = strtok(NULL, " ");
      if (text) {
        cmd_goal(text, 0);
      }
    } else if (strcmp(cmd, "plan_ready") == 0) {
      fm_state_handle_event(&g_session, FM_EVT_PLAN_READY);
    } else if (strcmp(cmd, "start") == 0) {
      fm_state_handle_event(&g_session, FM_EVT_START);
    } else if (strcmp(cmd, "task") == 0) {
      char *nstr = strtok(NULL, " ");
      int n = nstr ? atoi(nstr) : 0;
      if (n >= 1 && n <= g_session.stage_count &&
          !g_session.stages[n - 1].completed) {
        g_session.current_stage = n - 1;
        fm_state_handle_event(&g_session, FM_EVT_TASK_SELECTED);
      } else {
        printf("[FocusMate] task <1..%d>\n", g_session.stage_count);
      }
    } else if (strcmp(cmd, "duration") == 0) {
      char *minstr = strtok(NULL, " ");
      int minutes = minstr ? atoi(minstr) : 0;
      if (minutes == 25 || minutes == 30 ||
          minutes == 45 || minutes == 60) {
        g_session.total_minutes = minutes;
        fm_state_handle_event(&g_session, FM_EVT_DURATION_SELECTED);
      } else {
        printf("[FocusMate] duration <25|30|45|60>\n");
      }
    } else if (strcmp(cmd, "pause") == 0) {
      fm_state_handle_event(&g_session, FM_EVT_PAUSE);
    } else if (strcmp(cmd, "resume") == 0) {
      fm_state_handle_event(&g_session, FM_EVT_RESUME);
    } else if (strcmp(cmd, "removed") == 0) {
      phone_sensor_mock_removed(&g_session);
    } else if (strcmp(cmd, "returned") == 0) {
      phone_sensor_mock_returned(&g_session);
      } else if (strcmp(cmd, "stage_done") == 0 ||
                 strcmp(cmd, "round_end") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_ROUND_END);
      } else if (strcmp(cmd, "timeout") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_STAGE_TIMEOUT);
      } else if (strcmp(cmd, "round_continue") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_ROUND_CONTINUE);
      } else if (strcmp(cmd, "round_abandon") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_ROUND_ABANDON);
      } else if (strcmp(cmd, "home") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_HOME);
      } else if (strcmp(cmd, "library") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_LIBRARY_OPEN);
      } else if (strcmp(cmd, "library_count") == 0) {
        printf("[FocusMate] library unfinished count: %d\n",
               focus_storage_library_count());
      } else if (strcmp(cmd, "library_load") == 0) {
        char *nstr = strtok(NULL, " ");
        int n = nstr ? atoi(nstr) : -1;
        if (n >= 0 && focus_storage_load_library(n, &g_session) == 0) {
          fm_state_handle_event(&g_session, FM_EVT_RESTORE);
        } else {
          printf("[FocusMate] library_load <index>\n");
        }
      } else if (strcmp(cmd, "end_task") == 0 ||
                 strcmp(cmd, "exit_task") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_EXIT_REQUEST);
      } else if (strcmp(cmd, "exit_yes") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_EXIT_CONFIRM);
      } else if (strcmp(cmd, "exit_no") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_EXIT_CANCEL);
      } else if (strcmp(cmd, "review_none") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_REVIEW_NONE);
      } else if (strcmp(cmd, "review_done") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_REVIEW_DONE);
      } else if (strcmp(cmd, "settle") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_SETTLE_REQUEST);
      } else if (strcmp(cmd, "settle_yes") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_SETTLE_CONFIRM);
      } else if (strcmp(cmd, "settle_no") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_SETTLE_CANCEL);
      } else if (strcmp(cmd, "cancel") == 0) {
        fm_state_handle_event(&g_session, FM_EVT_CANCEL);
        focus_storage_clear();
      } else if (strcmp(cmd, "save") == 0) {
        focus_storage_save(&g_session);
        printf("[FocusMate] session saved\n");
      } else if (strcmp(cmd, "restore") == 0 ||
                 strcmp(cmd, "resume_last") == 0) {
        if (focus_storage_load(&g_session) == 0 &&
            g_session.stage_count > 0) {
          fm_state_handle_event(&g_session, FM_EVT_RESTORE);
        } else {
          printf("[FocusMate] nothing to restore\n");
        }
      } else {
        printf("Unknown command: %s (type 'help')\n", cmd);
      }
    }

  focus_ui_deinit();
  printf("FocusMate exiting\n");
  return 0;
}
