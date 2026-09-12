# FocusMate Demo v2 Specification

## Scope

This demo validates an Agent-assisted focus loop on openvela + ai_agent:

1. Capture a goal.
2. Ask the LLM to split it into at most four executable tasks.
3. Run one or more focus rounds.
4. After each round, ask whether a task was completed.
5. If yes, let the user select the completed task on the touchscreen.
6. At settlement, show only two facts: task completion and focus/interruption totals.
7. Store the full structured record locally.

Voice input is a later extension. The first implementation uses CLI text input and the same data contract.

## Input and controls

- KEY2 short press: context action (start, pause, resume, end current round, start next round).
- KEY2 long press (>= 1.5 s): request early settlement.
- Touchscreen: confirmation dialogs, task selection, review and settlement confirmation.
- CLI: development fallback for all actions.

## Planning contract

The LLM returns only JSON with one to four `stages`:

```json
{
  "goal": "complete the contest submission",
  "total_minutes": 60,
  "stages": [
    {"title": "整理提交要求", "minutes": 20},
    {"title": "完善 README", "minutes": 25},
    {"title": "最终检查", "minutes": 15}
  ]
}
```

Rules:

- Task count by duration: 2 tasks for <15 minutes, 3 for 15-29 minutes, 4 for >=30 minutes.
- At most four stages.
- Stage titles are short and actionable.
- Stage minutes must sum exactly to `total_minutes`.
- The app validates the response and falls back to a local plan on error.

## Round flow

```text
READY
  -> plan is shown first and waits for user confirmation
  -> KEY2 short / START
FOCUSING
  -> timer expiry or KEY2 short
REVIEWING
  show: "Did this round complete a task?"
    yes -> show up to four task rows, user selects one, confirm
    no  -> record no completion
  -> if all tasks completed: COMPLETED
  -> otherwise: READY for the next round
```

Only one task completion is selected per round in Demo v2. The record format leaves room for multiple IDs later.

## Early settlement

```text
long press KEY2
  -> SETTLE_CONFIRM
     touch: continue or settle
       continue -> restore previous state
       settle   -> REVIEWING once, then COMPLETED
```

Long press is ignored in `IDLE` and `COMPLETED`.

## Settlement report

The report does not call the LLM. The screen shows only:

```text
完成  2/4
专注  47 分钟 · 中断 1 次
```

The full session record stores task names, statuses, rounds, timings, interruptions, and early-exit status.

## Storage

Original facts and the final report are written to the FocusMate data directory. The report is generated from device facts, never from the LLM.
