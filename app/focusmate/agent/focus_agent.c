/****************************************************************************
 * app/focusmate/agent/focus_agent.c
 *
 * FocusMate <-> ai_agent bridge (M4/M9).
 *
 * flow:
 *   1. focus_agent_init() opens a velaclaw client to ai_agent.
 *   2. focus_agent_plan() sends the goal + focus-planner skill prompt to
 *      ai_agent; the agent (LLM) replies with a JSON plan.
 *   3. The JSON plan is parsed with cJSON into fm_session_t stages.
 *   4. If the agent is not connected, times out, or returns invalid JSON,
 *      focus_agent_plan() falls back to a local default plan.
 *
 * The plan is validated: 2..FM_MAX_STAGES stages, minutes sum == total.
 *
 * Build modes:
 *   - CONFIG_EXAMPLES_AI_AGENT_VELA: full velaclaw bridge to ai_agent.
 *   - otherwise: offline mode; always uses the local fallback plan.
 ****************************************************************************/

#include "focus_agent.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <unistd.h>

#include <netutils/cJSON.h>

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#include <nuttx/sched.h>
#include <velaclaw/client.h>
#endif

#define TAG "focus_agent"

#define PLAN_TIMEOUT_MS 20000
#define PLAN_REPLY_MAX 4096

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA

/* ── velaclaw client state ─────────────────────────────────────── */

static velaclaw_client_t *s_client;
static bool s_connected;

/* ── ask callback plumbing ─────────────────────────────────────── */

typedef struct {
  char reply[PLAN_REPLY_MAX];
  volatile bool got_json;   /* set when a valid plan JSON arrived */
  volatile bool got_final;  /* set when a non-status final reply arrived */
  volatile bool got_error;  /* set when the agent reported an error */
  volatile int status;
} ask_ctx_t;

/*
 * The agent pushes intermediate status messages ("正在思考中...") before
 * the final reply. We must not treat those as the answer: keep the last
 * non-empty text, and only mark got_json when it looks like a plan.
 */
static void ask_cb(int status, const char *text, void *cookie)
{
  ask_ctx_t *ctx = (ask_ctx_t *)cookie;
  if (!ctx) {
    return;
  }
  ctx->status = status;
  if (text && text[0]) {
    /* Heuristic: a plan JSON starts with '{' */
    const char *p = text;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') {
      p++;
    }
    if (*p == '{') {
      strncpy(ctx->reply, text, sizeof(ctx->reply) - 1);
      ctx->reply[sizeof(ctx->reply) - 1] = '\0';
      ctx->got_json = true;
      ctx->got_final = true;
    } else if (strstr(text, "正在思考") == NULL &&
               strstr(text, "正在分析") == NULL) {
      /* A non-status text reply (summary / answer) */
      strncpy(ctx->reply, text, sizeof(ctx->reply) - 1);
      ctx->reply[sizeof(ctx->reply) - 1] = '\0';
      ctx->got_final = true;
    }
  } else if (status != 0) {
    ctx->got_error = true;
  }
}

/* ── ai_agent process detection ────────────────────────────────── */

typedef struct {
  bool found;
} agent_probe_t;

static void agent_probe_cb(FAR struct tcb_s *tcb, FAR void *arg)
{
  agent_probe_t *probe = (agent_probe_t *)arg;
  if (probe->found || !tcb) {
    return;
  }
  if (tcb->name[0] != '\0' && strcmp(tcb->name, "ai_agent") == 0) {
    probe->found = true;
  }
}

/* True when an ai_agent task is actually running. */
static bool agent_task_running(void)
{
  agent_probe_t probe;
  memset(&probe, 0, sizeof(probe));
  nxsched_foreach(agent_probe_cb, &probe);
  return probe.found;
}

/* ── init / deinit ─────────────────────────────────────────────── */

int focus_agent_init(void)
{
  if (s_client) {
    return 0;
  }

  /*
   * velaclaw_client_open() succeeds even when ai_agent is not running
   * (the tap table is a static initializer), but velaclaw_ask() would
   * then push onto an uninitialised message bus and crash. So only
   * open the client when the ai_agent task is actually present.
   */
  if (!agent_task_running()) {
    syslog(LOG_INFO, "[%s] ai_agent not running (offline mode)\n", TAG);
    s_connected = false;
    s_client = NULL;
    return 0; /* not fatal: local fallback still works */
  }

  s_client = velaclaw_client_open("focusmate");
  if (!s_client) {
    syslog(LOG_WARNING, "[%s] velaclaw_client_open failed (offline mode)\n", TAG);
    s_connected = false;
    return 0; /* not fatal: local fallback still works */
  }
  s_connected = true;
  syslog(LOG_INFO, "[%s] connected to ai_agent\n", TAG);
  return 0;
}

bool focus_agent_is_connected(void)
{
  return s_connected && s_client != NULL;
}

#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */

/* ── Offline-mode stubs (no CONFIG_EXAMPLES_AI_AGENT_VELA) ─────── */

#ifndef CONFIG_EXAMPLES_AI_AGENT_VELA
int focus_agent_init(void)
{
  syslog(LOG_INFO, "[%s] offline build (no ai_agent)\n", TAG);
  return 0;
}

bool focus_agent_is_connected(void)
{
  return false; /* no ai_agent: always use local fallback */
}
#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */

/* ── JSON plan parsing (cJSON) ─────────────────────────────────── */

/*
 * Parse a JSON plan string into sess->stages.
 * Validates: goal, total_minutes, 2..FM_MAX_STAGES stages whose
 * minutes sum equals total_minutes. Returns 0 on success, -1 on any
 * invalid field (caller falls back to local plan).
 */
static int parse_plan_json(fm_session_t *sess, const char *json_str)
{
  cJSON *root, *goal, *total, *stages, *item;
  int i, sum = 0;

  if (!sess || !json_str || !json_str[0]) {
    return -1;
  }

  root = cJSON_Parse(json_str);
  if (!root) {
    syslog(LOG_WARNING, "[%s] plan JSON parse failed\n", TAG);
    return -1;
  }

  goal = cJSON_GetObjectItem(root, "goal");
  total = cJSON_GetObjectItem(root, "total_minutes");
  stages = cJSON_GetObjectItem(root, "stages");

  if (!cJSON_IsString(goal) || !cJSON_IsNumber(total) ||
      !cJSON_IsArray(stages)) {
    cJSON_Delete(root);
    return -1;
  }

  int total_min = (int)total->valuedouble;
  int n = cJSON_GetArraySize(stages);
  if (total_min <= 0 || n < 2 || n > FM_MAX_STAGES) {
    cJSON_Delete(root);
    return -1;
  }

  for (i = 0; i < n; i++) {
    item = cJSON_GetArrayItem(stages, i);
    if (!item) {
      cJSON_Delete(root);
      return -1;
    }
    cJSON *title = cJSON_GetObjectItem(item, "title");
    cJSON *minutes = cJSON_GetObjectItem(item, "minutes");
    if (!cJSON_IsString(title) || !cJSON_IsNumber(minutes) ||
        !title->valuestring[0] || (int)minutes->valuedouble <= 0) {
      cJSON_Delete(root);
      return -1;
    }
    strncpy(sess->stages[i].title, title->valuestring,
            sizeof(sess->stages[i].title) - 1);
    sess->stages[i].title[sizeof(sess->stages[i].title) - 1] = '\0';
    sess->stages[i].minutes = (int)minutes->valuedouble;
    sum += sess->stages[i].minutes;
  }

  /* minutes must sum exactly to total_minutes */
  if (sum != total_min) {
    syslog(LOG_WARNING, "[%s] plan minutes sum %d != total %d\n",
           TAG, sum, total_min);
    cJSON_Delete(root);
    return -1;
  }

  sess->stage_count = n;
  sess->total_minutes = total_min;
  strncpy(sess->goal, goal->valuestring, sizeof(sess->goal) - 1);
  sess->goal[sizeof(sess->goal) - 1] = '\0';

  cJSON_Delete(root);
  return 0;
}

/* ── local fallback plan ───────────────────────────────────────── */

/* Split the goal into 2-3 conservative stages. */
static int local_default_plan(fm_session_t *sess, const char *goal,
                              int total_minutes)
{
  int n = total_minutes < 30 ? 2 : 3;
  if (n > FM_MAX_STAGES) {
    n = FM_MAX_STAGES;
  }
  const char *default_titles[FM_MAX_STAGES] = {
    "整理思路",
    "主要工作",
    "完成整理"
  };
  int base = total_minutes / n;
  int rem = total_minutes % n;

  sess->stage_count = n;
  sess->total_minutes = total_minutes;
  strncpy(sess->goal, goal, sizeof(sess->goal) - 1);
  sess->goal[sizeof(sess->goal) - 1] = '\0';

  for (int i = 0; i < n; i++) {
    strncpy(sess->stages[i].title, default_titles[i],
            sizeof(sess->stages[i].title) - 1);
    sess->stages[i].title[sizeof(sess->stages[i].title) - 1] = '\0';
    sess->stages[i].minutes = base + (i < rem ? 1 : 0);
  }
  syslog(LOG_INFO, "[%s] using local fallback plan (%d stages)\n",
         TAG, n);
  return 0;
}

/* ── public API ────────────────────────────────────────────────── */

int focus_agent_plan(fm_session_t *sess, const char *goal, int total_minutes)
{
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
  char prompt[512];
  int ret;

  /* Try AI planner first when connected. */
  if (focus_agent_is_connected()) {
    snprintf(prompt, sizeof(prompt),
             "任务：把用户目标拆成专注计划。目标=\"%s\"，可用时间=%d 分钟。"
             "直接输出一个 JSON 对象，不要调用任何工具，不要输出 markdown "
             "代码块或任何解释文字。JSON 格式："
             "{\"goal\":\"...\",\"total_minutes\":%d,\"stages\":["
             "{\"title\":\"阶段1\",\"minutes\":N},...]}。"
             "要求：2-4 个阶段，各阶段 minutes 之和必须等于 total_minutes。",
             goal, total_minutes, total_minutes);

    ask_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    velaclaw_ask_req_t req;
    memset(&req, 0, sizeof(req));
    req.text = prompt;
    req.timeout_ms = PLAN_TIMEOUT_MS;

    ret = velaclaw_ask(s_client, &req, ask_cb, &ctx);
    if (ret < 0) {
      syslog(LOG_WARNING, "[%s] velaclaw_ask failed: %d\n", TAG, ret);
    } else {
      /* The agent may push status messages before the final reply;
       * keep waiting until a valid-looking JSON arrives or timeout. */
      int spins = 0;
      while (!ctx.got_json && !ctx.got_error && spins < 100) {
        usleep(200000);
        spins++;
      }
      if (ctx.got_json && parse_plan_json(sess, ctx.reply) == 0) {
        syslog(LOG_INFO, "[%s] AI plan OK (%d stages)\n", TAG,
               sess->stage_count);
        return 0;
      }
      syslog(LOG_WARNING, "[%s] AI plan invalid/missing, falling back\n", TAG);
    }
  }
#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */

  return local_default_plan(sess, goal, total_minutes);
}

int focus_agent_summarize(const fm_session_t *sess, char *out, int out_size)
{
#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
  char prompt[512];
  int ret;

  if (!out || out_size <= 0) {
    return -1;
  }

  if (focus_agent_is_connected()) {
    snprintf(prompt, sizeof(prompt),
             "用 50-80 字总结这次专注：目标=\"%s\"，计划 %d 分钟，"
             "中断 %d 次，共 %d 个阶段。只输出总结。",
             sess->goal, sess->total_minutes,
             sess->interrupt_count, sess->stage_count);

    ask_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));

    velaclaw_ask_req_t req;
    memset(&req, 0, sizeof(req));
    req.text = prompt;
    req.timeout_ms = PLAN_TIMEOUT_MS;

    ret = velaclaw_ask(s_client, &req, ask_cb, &ctx);
    if (ret >= 0) {
      int spins = 0;
      while (!ctx.got_final && !ctx.got_error && spins < 100) {
        usleep(200000);
        spins++;
      }
      if (ctx.got_final && ctx.reply[0]) {
        strncpy(out, ctx.reply, out_size - 1);
        out[out_size - 1] = '\0';
        return 0;
      }
    }
  }
#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */

  snprintf(out, out_size, "专注完成：%s，计划 %d 分钟，中断 %d 次，继续保持！",
           sess->goal, sess->total_minutes, sess->interrupt_count);
  return 0;
}
