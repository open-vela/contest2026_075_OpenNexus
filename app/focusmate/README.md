# FocusMate（AI 桌面专注伙伴）

> Contest 2026 team 075 OpenNexus — AI 硬件产品创新赛道

## 一句话定义

FocusMate 是一台面向学生和长期伏案人群的 AI 桌面专注设备：
用户放下手机并说出目标，设备将目标拆成专注计划，通过实体反馈和离席感知
陪伴执行，并在中断后主动帮助恢复任务。

## 当前状态（M3：应用骨架）

- 独立应用 `focusmate`（映射到编译树 `packages/demos/contest2026_075_focusmate`）
- 完整状态机：`IDLE → PLANNING → READY → FOCUSING ↔ PAUSED`、
  `FOCUSING → INTERRUPTED → RECOVERING → FOCUSING`、`→ COMPLETED`
- 事件驱动架构：UI / Timer / Sensor / Storage 只产生事件，状态机统一处理
- 模块划分：
  - `core/`     — 状态机（focus_state）、1s 计时（focus_timer）
  - `agent/`    — ai_agent 桥接（focus_agent，M4 接 focus-planner Skill）
  - `sensor/`   — 手机存在检测抽象（phone_sensor，当前 Mock）
  - `storage/`  — 会话持久化（focus_storage，当前简单文本文件，M8 改 cJSON/NOR）
  - `ui/`       — LVGL 三页 UI（focus_ui，M5 完善倒计时与进度）

## 构建与运行

```bash
# 在 openvela 工作区根目录
cd ~/openvela-workspace

# 启用 FocusMate（menuconfig: Demos → FocusMate）
./build.sh vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap --cmake menuconfig

# 编译
./build.sh vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap --cmake -j4

# 运行模拟器
./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap

# 模拟器内启动
nsh> focusmate
```

## 命令行（M3 调试用）

```
goal <text> <minutes>   提交目标与总时长（本地 fallback 生成 2-3 阶段计划）
start / pause / resume  开始 / 暂停 / 继续
removed / returned      Mock 手机取走 / 放回（触发主动暂停/恢复）
stage_done              模拟当前阶段结束
status                  查看状态
save / restore          保存 / 恢复会话
```

## 里程碑路线

- [x] M0 环境 / M1 ai_agent 基线 / M2 mini_memo Demo
- [x] M3 FocusMate 应用骨架与状态机
- [ ] M4 focus-planner 自定义 Skill 与结构化 JSON 计划
- [ ] M5 专注计时与界面（倒计时 / 进度条）
- [ ] M6 Mock 手机取走/放回 → 主动暂停/恢复（核心场景）
- [ ] M7 真实手机停靠检测（GPIO/ADC）
- [ ] M8 NOR 状态保存与任务恢复
- [ ] M9+ 真机集成 / PWM 灯光 / 语音 / 最终提交
