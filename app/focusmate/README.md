# FocusMate Application

FocusMate is an openvela application that combines ai_agent task planning,
multi-round focus timing, touchscreen review, and local history storage.

## Demo v2

- LLM plans at most four tasks.
- Each focus round ends in `REVIEWING`.
- The touchscreen asks whether a task was completed.
- If yes, the user selects the completed task.
- Settlement shows only:

```text
完成  2/4
专注  47 分钟 | 中断 1 次
```

The settlement report does not call the LLM.

## Modules

| Module | Purpose |
|---|---|
| `core/focus_state` | Ten-state FocusMate state machine |
| `core/focus_timer` | One-second focus ticker |
| `agent/focus_agent` | velaclaw client, JSON plan parsing, Skill deployment |
| `storage/focus_storage` | Session and history JSON |
| `ui/focus_ui` | LVGL main pages, touch review layer, board key input |

## Build

See the repository root `README.md`. The goldfish helper script prepares the
ai_agent defconfig, FocusMate options, and the velaclaw link fix.

## Test

Use the CLI commands documented in the root README. In a headless simulator,
the UI downgrades to CLI while the state machine and storage remain testable.
