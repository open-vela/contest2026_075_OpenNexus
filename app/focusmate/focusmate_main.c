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

  case FM_COMPLETED: {
    char summary[256];
    focus_agent_summarize(sess, summary, sizeof(summary));
    printf("[FocusMate] %s\n", summary);
    /* M8: archive the finished session, then clear the current one so a
     * restart does not offer to resume an already-finished task. */
    focus_storage_append_history(sess);
    focus_storage_clear();
    break;
  }

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
    stage_total = g_session.stages[g_session.current_stage].minutes * 60;
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
  case FM_UI_CMD_PAUSE:
    return "PAUSE";
  case FM_UI_CMD_RESUME:
    return "RESUME";
  case FM_UI_CMD_CANCEL:
    return "STOP";
  case FM_UI_CMD_QUICKSTART:
    return "QUICKSTART";
  default:
    return "?";
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
    "  agent                   show ai_agent connection status\n"
    "  demo                    run core active-scene demo\n"
    "  save / restore          save / load session\n"
    "  help                    this message\n"
    "  quit                    exit FocusMate\n");
}

/* Forward declarations */
static void cmd_goal(const char *text, int minutes);

/* M6: core active scene - user says nothing, device senses the phone
 * being taken away, auto-pauses, and on return proactively asks to
 * resume. This is the "主动 + 执行" scenario the contest requires. */
static void cmd_demo(void)
{
  printf("\n===== FocusMate 主动场景演示 =====\n");
  printf("[1] 用户放置手机，开始专注...\n");
  if (g_session.state != FM_READY) {
    if (g_session.state != FM_IDLE) {
      fm_state_handle_event(&g_session, FM_EVT_CANCEL);
    }
    cmd_goal("完成比赛演示 3", 3);
  }
  fm_state_handle_event(&g_session, FM_EVT_START);
  printf("    (专注进行中...)\n");
  sleep(3);

  printf("\n[2] 用户不说话，直接取走手机 → 设备感知并自动暂停\n");
  phone_sensor_mock_removed(&g_session);
  sleep(2);

  printf("\n[3] 用户放回手机 → 设备主动提示恢复\n");
  phone_sensor_mock_returned(&g_session);
  sleep(2);

  printf("\n[4] 用户确认恢复 → 从原阶段继续专注\n");
  fm_state_handle_event(&g_session, FM_EVT_RESUME);
  sleep(2);
  fm_state_handle_event(&g_session, FM_EVT_CANCEL);
  printf("===== 演示结束 =====\n");
}

static void cmd_goal(const char *text, int minutes)
{
  if (minutes <= 0) {
    minutes = 45;
  }
  /* A fresh goal starts a fresh session: reset counters from any
   * previous (possibly interrupted) session. */
  g_session.interrupt_count = 0;
  g_session.elapsed_seconds = 0;
  g_session.stage_elapsed_seconds = 0;
  g_session.current_stage = -1;
  g_session.state = FM_IDLE;

  /* M4: focus_agent_plan() tries the AI focus-planner skill first,
   * falls back to a local default plan when offline/invalid. */
  printf("[FocusMate] planning goal: %s (%d min)...\n", text, minutes);
  focus_agent_plan(&g_session, text, minutes);
  printf("[FocusMate] plan ready: %d stages, %d min total\n",
         g_session.stage_count, g_session.total_minutes);
  for (int i = 0; i < g_session.stage_count; i++) {
    printf("  [%d/%d] %s (%d min)\n", i + 1, g_session.stage_count,
           g_session.stages[i].title, g_session.stages[i].minutes);
  }
  fm_state_handle_event(&g_session, FM_EVT_GOAL_SUBMITTED);
  fm_state_handle_event(&g_session, FM_EVT_PLAN_READY);
}

int main(int argc, char *argv[])
{
  printf("FocusMate starting (M3 skeleton)\n");

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

  /* M8: try to restore an unfinished session saved across restarts. */
  if (focus_storage_load(&g_session) == 0 && g_session.stage_count > 0) {
    printf("\n[FocusMate] 检测到上一次专注任务尚未完成！\n");
    printf("[FocusMate]     目标: %s\n",
           g_session.goal[0] ? g_session.goal : "(无目标)");
    if (g_session.current_stage >= 0 &&
        g_session.current_stage < g_session.stage_count) {
      printf("[FocusMate]     进行到: %s (第 %d/%d 阶段)，已专注 %d 秒\n",
             g_session.stages[g_session.current_stage].title,
             g_session.current_stage + 1, g_session.stage_count,
             g_session.elapsed_seconds);
    }
    printf("[FocusMate]     输入 resume 继续，cancel 放弃。\n");
    fm_state_handle_event(&g_session, FM_EVT_RESTORE);
  } else {
    fm_state_init(&g_session);
  }

  printf("[FocusMate] state -> %s\n", fm_state_name(g_session.state));
  focus_ui_refresh(&g_session);
  print_usage();

  char line[256];
  while (1) {
    fm_ui_cmd_t ucmd;

    /* A tap on the panel.  The button callback only leaves a request behind;
     * the state machine is driven from here so that every transition and
     * every storage write stays on this one thread. */
    ucmd = focus_ui_take_command();
    if (ucmd != FM_UI_CMD_NONE) {
      printf("\n[FocusMate] screen: %s\n", ui_cmd_name(ucmd));
      fflush(stdout);
      switch (ucmd) {
      case FM_UI_CMD_START:
        fm_state_handle_event(&g_session, FM_EVT_START);
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
      case FM_UI_CMD_QUICKSTART:
        /* No session is planned (fresh boot, or right after a long press
         * cancelled one), so start a default one rather than doing nothing.
         * This is what makes the key work in every state. */
        cmd_goal("专注", 25);
        fm_state_handle_event(&g_session, FM_EVT_START);
        break;
      default:
        break;
      }
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
      printf("state=%s goal='%s' stage=%d/%d elapsed=%d stage_sec=%d interrupts=%d\n",
             fm_state_name(g_session.state), g_session.goal,
             g_session.current_stage + 1, g_session.stage_count,
             g_session.elapsed_seconds, g_session.stage_elapsed_seconds,
             g_session.interrupt_count);
    } else if (strcmp(cmd, "agent") == 0) {
      printf("ai_agent connected: %s\n",
             focus_agent_is_connected() ? "yes" : "no");
    } else if (strcmp(cmd, "demo") == 0) {
      cmd_demo();
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

  focus_ui_deinit();
  printf("FocusMate exiting\n");
  return 0;
}
