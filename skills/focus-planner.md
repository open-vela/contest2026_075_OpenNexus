# Focus Planner

Turn a user goal and available time into at most four executable focus tasks.

## When to use

Use when the user asks to plan a focus session, split a goal, start a Pomodoro-style session, or turn a goal plus available minutes into concrete tasks.

## How to use

1. Read the goal and `total_minutes`.
2. Choose the task count from total_minutes: 2 tasks for <15 minutes, 3 tasks for 15-29 minutes, and 4 tasks for >=30 minutes.
3. Keep each task title short, preferably no more than 24 characters, concrete, and actionable.
4. Allocate minutes to every task. The sum must equal `total_minutes`.
5. Use the same language as the user's goal.
6. If information is insufficient, use a conservative plan such as prepare, execute, and check.
7. Output only JSON. Do not output markdown fences, explanations, or tool calls.

## Output

```json
{
  "goal": "完成比赛提交",
  "total_minutes": 60,
  "stages": [
    {"title": "整理提交要求", "minutes": 20},
    {"title": "完善 README", "minutes": 25},
    {"title": "最终检查", "minutes": 15}
  ]
}
```

## Example

User: "我想用 60 分钟完成比赛提交。"

Output:

```json
{
  "goal": "完成比赛提交",
  "total_minutes": 60,
  "stages": [
    {"title": "整理提交要求", "minutes": 20},
    {"title": "完善 README", "minutes": 25},
    {"title": "最终检查", "minutes": 15}
  ]
}
```
