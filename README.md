# FocusMate — AI 桌面专注伙伴

> 2026 首届 openvela AI 硬件开发者大赛
> 赛道：AI 硬件产品创新
> 队伍：contest2026_075_OpenNexus

FocusMate 是一个运行在 openvela + ai_agent 上的主动式桌面专注设备。用户在待机界面输入目标，Agent 将目标拆解为最多四个可执行任务，设备随后用多轮专注计时帮助用户执行。每轮结束后，用户通过触屏确认本轮完成的任务；结算时只显示客观结果：

```text
完成  2/4
专注  47 分钟 | 中断 1 次
```

## Demo v2 范围

- LLM 只负责把目标拆解为 1-4 个任务。
- 结算不调用 LLM，不生成长篇分析。
- 每轮结束进入 `REVIEWING`，询问本轮是否完成任务。
- 选择“是”后，在触屏上从最多四个任务中选择一个完成项。
- 选择“否”则直接进入下一轮或结算。
- 长按实体 `KEY2` 1.5 秒发起提前结算，触屏进行二级确认。
- 完整事实记录写入本地 JSON，屏幕只显示两行结算报告。
- 语音输入作为后续扩展；当前 Demo 使用 CLI 文本输入。

## 核心流程

```text
IDLE
  -> 输入目标
PLANNING
  -> LLM focus-planner Skill
READY
  -> KEY2 / 触屏开始
FOCUSING
  -> 计时 / 暂停 / 中断
REVIEWING
  -> 本轮是否完成任务?
     是 -> 触屏选择任务 -> 标记完成
     否 -> 跳过
  -> 全部完成: COMPLETED
  -> 未全部完成: READY，开始下一轮
COMPLETED
  -> 显示两行事实报告
  -> 写入 history.json
```

## 状态机

- `IDLE`
- `PLANNING`
- `READY`
- `FOCUSING`
- `PAUSED`
- `INTERRUPTED`
- `RECOVERING`
- `REVIEWING`
- `SETTLE_CONFIRM`
- `COMPLETED`

## 目录结构

```text
contest2026_075_OpenNexus/
├── app/focusmate/                  # FocusMate 应用
│   ├── core/                       # 状态机与计时器
│   ├── agent/                      # ai_agent/velaclaw 与 Skill 部署
│   ├── sensor/                     # 手机存在检测抽象
│   ├── storage/                    # session/history JSON
│   └── ui/                         # LVGL 界面与触屏任务选择层
├── skills/focus-planner.md         # 设备端自定义 Skill 源文件
├── tools/
│   ├── enable_goldfish_focusmate.sh
│   └── generate_focus_skill_header.py
├── docs/
│   ├── DEMO_V2_SPEC.md
│   ├── USER_GUIDE_zh-cn.md
│   └── board_bringup/
└── logs/                           # AI Coding 日志
```

`app/focusmate` 通过 manifest 的 `<linkfile>` 映射到 `packages/demos/contest2026_075_focusmate`。

## Skill

`skills/focus-planner.md` 是 v2 Skill：

- 只处理专注计划和任务拆解。
- 输出 1-4 个任务。
- 每项任务包含短标题和分钟数。
- 所有任务分钟数之和必须等于总时长。
- 只输出 JSON，不输出解释。
- 应用启动时会把该 Skill 自动写入 `/data/agent/skills/focus-planner.md`。

## 构建 goldfish

先准备官方 goldfish ai_agent 配置和公共仓修复：

```bash
cd ~/openvela
./contest2026_075_OpenNexus/tools/enable_goldfish_focusmate.sh
rm -rf cmake_out/vela_goldfish-arm64-v8a-ap
./build.sh vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap --cmake -j8
```

`enable_goldfish_focusmate.sh` 会：

1. 复制官方 ai_agent goldfish defconfig。
2. 启用 `CONFIG_LVX_USE_DEMO_FOCUSMATE`。
3. 启用 FocusMate 所需字体和数据目录。
4. 应用 `docs/board_bringup/ai_agent_velaclaw_link_fix.patch`。

运行模拟器：

```bash
./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap -no-window
```

## 常用命令

```text
goal <文字> <分钟>       输入目标并生成计划
start                     开始当前轮
round_end                 结束当前轮，进入 REVIEWING
review_done <1..4>        本轮完成第 N 项任务
review_none               本轮没有完成拆解任务
settle                    请求提前结算
settle_yes / settle_no    确认或取消结算
status                    查看状态
agent                     查看 ai_agent 连接状态
demo                      运行核心主动场景演示
quit                      退出
```

## 结算记录

当前 session 保存在 `/data/focusmate/session.json`，完成记录写入 `/data/focusmate/history.json`。

历史记录包含：

- 原始目标
- 任务标题与分钟数
- 每项任务是否完成
- 专注时长
- 中断次数
- 专注轮数
- 完成项数量
- 是否提前结算

示例：

```json
{
  "history": [
    {
      "goal": "完成比赛提交",
      "total_minutes": 6,
      "elapsed_seconds": 0,
      "interrupt_count": 0,
      "stage_count": 2,
      "round_count": 2,
      "completed_task_count": 2,
      "early_exit": false,
      "stages": [
        {"title": "整理思路", "minutes": 3, "completed": true},
        {"title": "主要工作", "minutes": 3, "completed": true}
      ]
    }
  ]
}
```

## openvela + ai_agent

应用使用 `velaclaw` 客户端调用 ai_agent。goldfish 构建需要 `CONFIG_EXAMPLES_AI_AGENT_VELA=y`。

公共仓 `packages_ai_agent` 当前需要补编译 `src/sdk/velaclaw_client_local.c`，修复补丁见：

`docs/board_bringup/ai_agent_velaclaw_link_fix.patch`

## 当前限制

- 真实 LLM 验证需要有效模型额度和网络。
- SF32LB52 板载有 MEMS 麦克风，但 openvela 音频采集驱动尚未完成适配。
- 语音输入暂列为后续扩展，当前交互使用文本和触屏。
- 触屏任务选择 UI 已实现，需在目标硬件上验证触摸控制器事件。

## AI Coding 日志

AI Coding 日志位于 `logs/<github_login>/`，提交前运行官方校验：

```bash
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/
```
