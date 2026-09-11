# FocusMate Developer Note — States & Transitions

Eight states, all driven by `fm_state_handle_event()` (core/focus_state.c).

| # | State | Meaning | Enters via event |
|---|-------|---------|------------------|
| 1 | `IDLE` | waiting for goal, or reset point | `CANCEL` (from most states), `PLAN_FAILED` |
| 2 | `PLANNING` | AI/local planner is building the plan | `GOAL_SUBMITTED` |
| 3 | `READY` | plan ready, waiting to start | `PLAN_READY` |
| 4 | `FOCUSING` | actively focusing on a stage | `START`; `RESUME` (from PAUSED/RECOVERING); `STAGE_TIMEOUT` when a stage remains |
| 5 | `PAUSED` | user paused manually | `PAUSE` |
| 6 | `INTERRUPTED` | phone taken away; auto-paused | `PHONE_REMOVED` |
| 7 | `RECOVERING` | phone returned, prompt to resume | `PHONE_RETURNED`; `RESTORE` (saved session) |
| 8 | `COMPLETED` | all stages finished | `STAGE_TIMEOUT`/`SESSION_FINISHED` on last stage |

Flow: `IDLE → PLANNING → READY → FOCUSING ↔ PAUSED`,
`FOCUSING → INTERRUPTED → RECOVERING → FOCUSING`, `FOCUSING → COMPLETED`.
`CANCEL` returns any active state to `IDLE`; `RESTORE` resumes a saved session at `RECOVERING`.
