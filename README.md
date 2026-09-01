# FocusMate — AI 桌面专注伙伴

> **2026 首届 openvela AI 硬件开发者大赛 · AI 硬件产品创新赛道**
> 队伍：contest2026_075_OpenNexus

## 一、作品简介

FocusMate 是一台运行在 **openvela + ai_agent** 上的主动式 AI 桌面专注设备。
面向学生和长期伏案人群：用户放下手机并说出目标，FocusMate 借助 AI 将目标
拆解为 2-4 个可执行的专注阶段并计时执行；当检测到手机被取走（离席）时，
设备**自动暂停并保存现场**；手机放回后，设备**主动询问是否继续**，从原阶段
原剩余时间恢复专注；任务完成后生成专注总结。

它不是"能聊天的番茄钟"——核心是 **AI 理解目标并形成计划，设备随后持续执行，
并在环境事件发生时主动改变状态和反馈**（"主动 + 执行"）。

## 二、选题方向

**AI 硬件产品创新**。作品完整覆盖赛道硬指标：

| 赛道要求 | FocusMate 实现 |
|---|---|
| openvela + ai_agent 运行 | ✅ goldfish-arm64-v8a-ap 模拟器验证通过 |
| LLM 后端 + 基本对话 | ✅ 小米 MiMo（`router_set mimo` + `ask`） |
| CLI 交互渠道 | ✅ NSH/vela CLI（`focusmate>` 命令集） |
| 设备端自定义 Skill | ✅ `focus-planner`：目标 → 结构化 JSON 计划 |
| **主动 + 执行场景** | ✅ 手机取走自动暂停；放回主动询问恢复 |
| 用户故事/功能清单/技术说明 | ✅ 本文档 + `app/focusmate/README.md` |

## 三、目录结构

```text
contest2026_075_OpenNexus/
├── app/focusmate/            # FocusMate 应用（主要作品代码）
│   ├── focusmate_main.c      # 入口 + CLI（含 demo 一键演示）
│   ├── core/                 # 状态机 + 1s 计时器
│   │   ├── focus_state.{c,h} # 8 状态 / 12 事件状态机
│   │   └── focus_timer.{c,h} # 真实 1 秒 tick、阶段自动切换
│   ├── agent/                # ai_agent 桥接
│   │   └── focus_agent.{c,h} # velaclaw 接入、JSON 计划解析、本地 fallback
│   ├── sensor/               # 手机存在检测抽象（当前 Mock，可换 GPIO/ADC）
│   │   └── phone_sensor.{c,h}
│   ├── storage/              # 会话持久化（重启恢复）
│   │   └── focus_storage.{c,h}
│   └── ui/                   # LVGL 三页界面（待机/专注/完成）
│       └── focus_ui.{c,h}
├── skills/
│   └── focus-planner.md      # 设备端自定义 Skill
├── app/hello_app/            # 官方样例骨架（可忽略）
├── quickapp/hello_quickapp/  # 官方样例骨架（可忽略）
├── board/contest_board/      # 官方样例骨架（可忽略）
├── docs/                     # （规划中）演示记录
└── logs/                     # AI Coding 日志
```

manifest（`contest2026_075_OpenNexus.xml`）已通过 `<linkfile>` 将
`app/focusmate` 映射到编译树 `packages/demos/contest2026_075_focusmate`。

## 四、运行方式

### 1. 拉取完整工程

```bash
mkdir -p ~/openvela-workspace && cd ~/openvela-workspace
repo init -u https://github.com/open-vela/contest2026_075_OpenNexus \
  -b dev-ai-contest-2026 -m contest2026_075_OpenNexus.xml
repo sync -c -j8
```

### 2. 编译（goldfish ARM64 模拟器配置）

```bash
cd ~/openvela-workspace
# defconfig 已启用 ai_agent + FocusMate（见 vendor/.../goldfish-arm64-v8a-ap/defconfig）
./build.sh vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap --cmake -j$(nproc)
```

> 说明：本项目构建于 `dev-ai-contest-2026` 分支官方 ai_agent 预置 defconfig
> 之上，仅追加 `CONFIG_LVX_USE_DEMO_FOCUSMATE=y` 等选项；如需从干净状态复现，
> 可先 cp `packages/ai_agent/defconfigs/goldfish-arm64-v8a-ap/goldfish-arm64-v8a-ap_defconfig`
> 到 board configs 目录再启用 FocusMate。

### 3. 运行模拟器

```bash
./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap -no-window
```

### 4. 使用流程

```bash
nsh> focusmate                    # 启动 FocusMate

focusmate> goal 完成比赛README 60   # 输入目标 + 可用分钟 → AI/本地生成计划
focusmate> start                    # 开始专注（每秒倒计时 + 进度）
focusmate> removed                  # 模拟手机被取走 → 自动暂停 + 保存现场
focusmate> returned                 # 模拟手机放回 → 主动询问是否继续
focusmate> resume                   # 从原阶段继续专注
focusmate> demo                     # 一键演示完整"主动+执行"闭环
```

> 若要启用真实 LLM 计划（而非本地 fallback），先在 vela> 配置：
> `set_proxy 10.0.2.2 7898`、`router_set mimo <api_key>`、
> `router_model 0 mimo-v2.5`，然后 focusmate 会自动通过 focus-planner 生成计划。

### 5. 核心演示（演示视频素材）

```text
用户放下手机，说"我想在一个小时内完成比赛选题"
  → AI 拆出 2-4 个阶段，屏幕显示计划
  → 用户开始专注，倒计时 + 进度条
  → 用户不说话直接拿走手机
  → 设备检测到离席：自动暂停 + 保存 + 主动提醒
  → 用户放回手机
  → 设备主动询问"欢迎回来，是否继续？"
  → 用户确认，从原阶段继续
  → 全部完成，生成专注总结
```

## 五、AI Coding 使用说明

本项目全程使用 AI 辅助开发（本会话即由 DSH/AI 编码助手执行）：

- **环境搭建与排障**：openvela 首次构建的 LFS 大文件下载、CMake 构建修复、
  MiMo 模型名适配（`mimo-v2-flash` → `mimo-v2.5`）、代理网络隧道等，均由
  AI 分析日志定位根因并给出修复方案。
- **架构设计**：状态机（8 状态 / 12 事件）、模块划分（core/agent/sensor/
  storage/ui）由 AI 按竞赛文档拆解为可独立验证的里程碑（M3-M6）。
- **编码**：FocusMate 各模块源码（约 1700 行）由 AI 生成并经编译、模拟器
  实测验证；发现并修复了 timer 线程重启、状态计数、velaclaw 链接缺失
  （官方 ai_agent CMakeLists bug）等问题。
- **验证**：每一步都在 goldfish 模拟器内实测（状态转换、倒计时、暂停/恢复、
  主动场景闭环），0 崩溃。

完整对话日志见 `logs/tofreemyself/`。

## 附：技术架构摘要

```text
用户/传感器事件 ──► 事件队列 ──► 状态机(focus_state)
                                      │ 状态迁移
                 ┌────────────────────┼────────────────────┐
                 ▼                    ▼                    ▼
             focus_timer          focus_agent          focus_storage
            (1s tick/阶段)      (velaclaw/LLM计划)     (持久化/恢复)
                 │                    │                    │
                 └────────────────────┼────────────────────┘
                                      ▼
                                  UI/CLI 反馈
```
