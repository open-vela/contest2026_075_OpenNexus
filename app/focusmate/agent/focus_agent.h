/****************************************************************************
 * app/focusmate/agent/focus_agent.h
 *
 * FocusMate <-> ai_agent bridge.
 *
 * The AI (focus-planner skill / LLM) generates structured plans; the device
 * parses and executes them. This module wraps the velaclaw client so the
 * rest of FocusMate only sees fm_agent_plan().
 ****************************************************************************/

#ifndef FOCUS_AGENT_H
#define FOCUS_AGENT_H

#include "core/focus_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the agent bridge (opens velaclaw client). Returns 0 on OK. */
int focus_agent_init(void);

/* True when the agent bridge is connected. */
bool focus_agent_is_connected(void);

/*
 * Ask the agent to convert goal + total_minutes into a structured plan.
 * On success fills sess->stages and returns 0. When the agent is
 * unavailable this falls back to a local default plan (M4).
 */
int focus_agent_plan(fm_session_t *sess, const char *goal, int total_minutes);

/* Ask the agent for a short completion summary (M6+). */
int focus_agent_summarize(const fm_session_t *sess, char *out, int out_size);

#ifdef __cplusplus
}
#endif

#endif /* FOCUS_AGENT_H */
