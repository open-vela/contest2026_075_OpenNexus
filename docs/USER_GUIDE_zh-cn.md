# FocusMate Demo v2 使用手册

本文档对应当前 `codex/llm-submission` 分支的 Demo v2。

## 1. 产品流程

```text
输入目标
→ LLM 拆解为 1-4 个任务
→ 开始一轮专注
→ 本轮结束
→ 触屏询问是否完成任务
   → 是: 选择本轮完成的任务
   → 否: 跳过
→ 继续下一轮或结算
→ 显示两行结算报告
→ 写入 history.json
```

结算报告只显示：

```text
完成  2/4
专注  47 分钟 | 中断 1 次
```

结算不调用 LLM，也不生成长篇总结。

## 2. 按键与触屏

| 操作 | 行为 |
|---|---|
| `KEY2` 短按 | 当前状态的主操作：开始、继续、结束本轮 |
| `KEY2` 长按 1.5 秒 | 发起提前结算 |
| 触屏 | 确认结算、选择完成的任务、执行界面按钮 |
| `KEY1` | 预留给后续语音输入/快捷键 |

提前结算流程：

```text
长按 KEY2
→ 触屏显示“结束今日专注?”
→ 选择“继续专注”或“结算”
→ 结算前确认任务完成情况
→ 显示两行报告并写入记录
```

## 3. 当前构建环境

- 工作区：`~/openvela`
- 团队仓：`~/openvela/contest2026_075_OpenNexus`
- 模拟器目标：`goldfish-arm64-v8a-ap`
- 目标真机：`SF32LB52-DevKit-LCD`

构建 goldfish：

```bash
cd ~/openvela
./contest2026_075_OpenNexus/tools/enable_goldfish_focusmate.sh
rm -rf cmake_out/vela_goldfish-arm64-v8a-ap
./build.sh vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap --cmake -j8
```

运行模拟器：

```bash
./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap -no-window
```

## 4. CLI 调试命令

```text
goal <文字> <分钟>        输入目标，调用 focus-planner Skill
start                      开始当前轮
end / round_end            结束当前轮，进入 REVIEWING
review_done <1..4>         本轮完成第 N 项任务
review_none                本轮没有完成任务
settle                     请求提前结算
settle_yes / settle_no     确认或取消结算
status                     查看状态、轮次和完成数量
agent                      查看 ai_agent 连接状态
save / restore             手动保存或恢复当前 session
cancel                     放弃当前 session
quit                       退出 FocusMate
```

`stage_done` 仍保留为 `round_end` 的兼容写法。

## 5. 数据文件

```text
/data/focusmate/session.json   当前未完成 session
/data/focusmate/history.json   已完成 session 历史
/data/agent/skills/focus-planner.md  应用启动时自动部署的 Skill
```

历史记录包括目标、任务名称、任务状态、计划时长、实际专注秒数、轮数、中断次数和是否提前结算。

## 6. 语音扩展状态

- SF32LB52 硬件带 MEMS MIC 和音频功放。
- 当前 openvela 板级支持尚未完成麦克风采集驱动。
- ai_agent 已有云端 ASR/TTS 接口。
- 语音输入暂列后续阶段，当前 Demo 使用 CLI 文本和触屏。

## 7. AI Coding 日志

AI Coding 日志位于：

```text
logs/<github_login>/
```

提交前校验：

```bash
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/
```
