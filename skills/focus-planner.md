# Focus Planner

Convert a user goal + available time into an executable focus plan.

## Trigger

Use this skill when the user wants to plan a focused work session, e.g.
"我想在一个小时内完成比赛选题" or "45 分钟修改论文引言".

## Input

- goal: the task/goal the user wants to accomplish
- total_minutes: available time in minutes (optional, default 45)

## Output

Respond with **only** a JSON object, no markdown fences, no explanation:

```json
{
  "goal": "完成比赛选题",
  "total_minutes": 60,
  "stages": [
    {"title": "整理需求与限制", "minutes": 25},
    {"title": "比较并确定方案", "minutes": 25},
    {"title": "记录结论", "minutes": 10}
  ]
}
```

## Rules

1. Output only JSON, nothing else.
2. 2-4 stages; each stage must be an actionable step derived from the goal.
3. Sum of stage minutes MUST equal total_minutes.
4. If the user gives too little information, use a conservative default
   plan (e.g. 3 equal stages: 准备/执行/收尾).
5. Stage titles should be short (<= 24 chars) and in the same language as
   the user's goal.
