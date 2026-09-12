# Focus Planner

Turn a user goal into at most four executable focus tasks.

## When to use

Use when the user asks to plan a focus session, split a goal, start a Pomodoro-style session, or turn a goal into concrete tasks.

## How to use

1. Read the user's goal. Do not ask for or infer an overall time budget.
2. Decide the task count from the goal's complexity, using 2 to 4 tasks. Do not choose the number from a time limit.
3. Keep each task title short, preferably no more than 24 characters, concrete, and actionable.
4. Do not predict task duration and do not allocate minutes to tasks. The focus round length is selected by the user on the device.
5. Order tasks in the sequence the user should execute them.
6. Use the same language as the user's goal.
7. If information is insufficient, use a conservative plan such as prepare, execute, and check.
8. Output only JSON. Do not output markdown fences, explanations, or tool calls.

## Output

```json
{
  "goal": "完成比赛提交",
  "stages": [
    {"title": "整理提交要求"},
    {"title": "完善 README"},
    {"title": "最终检查"}
  ]
}
```

## Example

User: "我想完成比赛提交。"

Output:

```json
{
  "goal": "完成比赛提交",
  "stages": [
    {"title": "整理提交要求"},
    {"title": "完善 README"},
    {"title": "最终检查"}
  ]
}
```
