# FocusMate 当前 UI 结构

本文件用于快速查看和修改 FocusMate 的界面结构。当前运行时界面由 LVGL C 代码直接创建，源文件是：

`app/focusmate/ui/focus_ui.c`

机器可读版本：`docs/focusmate_ui_structure.json`

## 1. 画布

- 参考逻辑尺寸：390 x 450
- 背景色：`#101418`
- 主字体：`lv_font_simsun_24_cjk`
- 大数字字体：`lv_font_montserrat_40`
- 标题字体：`lv_font_montserrat_32`

## 2. 图层树

```text
screen
├─ title_label                  "FocusMate"
├─ state_label                  当前状态，例如 READY / FOCUSING
├─ timer_label                  倒计时，仅在专注相关状态显示
├─ progress_bar                 当前阶段进度，仅在专注相关状态显示
├─ info_label                   主提示文字，多行居中
├─ primary_button               主操作按钮
├─ stop_button                  次操作按钮
└─ review_overlay               本轮任务确认弹层
   ├─ review_title              "选择本轮完成的任务"
   ├─ task_button_1             未完成任务 1
   ├─ task_button_2             未完成任务 2
   ├─ task_button_3             未完成任务 3
   └─ task_button_4             未完成任务 4
```

## 3. 元素定义

| ID | C 变量 | 类型 | 位置/尺寸 | 默认样式 | 主要文本来源 |
|---|---|---|---|---|---|
| `screen` | `scr` | screen | 390 x 450 | 背景 `#101418` | - |
| `title` | `s_title_label` | label | 顶部居中，y=10 | Montserrat 32，白色 | `FocusMate` |
| `state` | `s_state_label` | label | 顶部居中，y=52 | Montserrat 20，状态色 | `fm_state_name()` |
| `timer` | `s_timer_label` | label | 顶部居中，y=84 | Montserrat 40，白色 | `mm:ss` |
| `progress` | `s_bar` | bar | 顶部居中，y=142，330 x 16 | 默认 bar | 当前阶段百分比 |
| `info` | `s_info_label` | label | 顶部居中，y=180，宽 360 | CJK 24，`#d8dee9`，居中，行距 10，自动换行 | 按状态生成 |
| `primary` | `s_btn_primary` | button + label | 左下，x=30，y=-44，140 x 56 | 圆角 14，文字 20 | 按状态变化 |
| `secondary` | `s_btn_stop` | button + label | 右下，x=-30，y=-44，140 x 56 | 圆角 14，文字 20 | 按状态变化 |
| `review_modal` | `s_review_overlay` | panel | 居中，360 x 300 | `#1b222b`，圆角 16 | - |
| `review_title` | `s_review_title` | label | 顶部居中，y=8 | CJK 24，白色 | `选择本轮完成的任务` |
| `review_task_n` | `s_review_task_btns[n]` | button | 顶部居中，y=48+n*58，320 x 48 | 默认按钮 | 未完成任务标题 |

## 4. 状态与界面映射

| 状态 | timer/bar | 主提示 | primary | secondary | review overlay |
|---|---|---|---|---|---|
| `IDLE` | 隐藏 | 把手机放好 / 想好要做的事 / 点一下就好 | `START` 绿色 | 隐藏 | 隐藏 |
| `PLANNING` | 隐藏 | `AI 正在安排...` | 隐藏 | 隐藏 | 隐藏 |
| `READY` | 隐藏 | `AI 已拆解为 N 项 / 共 X 分钟 / 点击 START 开始第一项` | `START` 绿色 | 隐藏 | 隐藏 |
| `FOCUSING` | 显示 | `当前项 / 百分比 / 已专注 mm:ss / 中断 N 次` | `END` 蓝色 | `PAUSE` 橙色 | 隐藏 |
| `PAUSED` | 显示 | 同 `FOCUSING` 文案 | `RESUME` 绿色 | `END` 灰色 | 隐藏 |
| `INTERRUPTED` | 显示 | 手机被取走 / 当前任务 / 中断次数 / 点一下接着做 | `RESUME` 绿色 | `END` 灰色 | 隐藏 |
| `RECOVERING` | 显示 | 你回来了 / 之前任务 / 点一下接着做 | `RESUME` 绿色 | `END` 灰色 | 隐藏 |
| `REVIEWING` | 隐藏 | `本轮是否完成拆解任务? / 是: 选择完成任务 / 否: 跳过本轮` | `YES` 绿色 | `NO` 灰色 | 默认隐藏；进入选择模式后显示 |
| `SETTLE_CONFIRM` | 隐藏 | `结束今日专注? / 结算 / 继续专注` | `SETTLE` 红色 | `CONTINUE` 绿色 | 隐藏 |
| `COMPLETED` | 隐藏 | `完成 N/M / 专注 X 分钟 | 中断 N 次` | `NEW` 绿色 | 隐藏 | 隐藏 |

说明：`REVIEWING` 先显示 YES/NO；点 YES 后进入选择模式，隐藏底部按钮并显示 `review_overlay`。弹层只列出尚未完成的任务。

## 5. 操作入口

| 操作 | 行为 |
|---|---|
| 点击 primary | 提交 `primary_button.cmd` |
| 点击 secondary | 提交 `secondary_button.cmd` |
| 短按 KEY2 | 执行当前状态的 primary 命令 |
| 长按 KEY2 | 请求提前结算，进入 `SETTLE_CONFIRM` |
| 点击任务按钮 | 选中本轮完成的任务，回到 `READY` |

## 6. 修改位置

- 增删控件：`ui_build()`
- 元素位置、大小、颜色、圆角：`ui_build()`、`btn_make()`
- 状态文案、按钮文字和命令：`ui_apply()`
- 弹层任务按钮：`review_apply()`
- 状态枚举：`core/focus_state.h`
- 状态切换逻辑：`core/focus_state.c`

## 7. 注意事项

1. 当前 UI 不是由 JSON 自动生成，修改 `focusmate_ui_structure.json` 只表示结构设计发生变化，运行时还需要同步修改 `focus_ui.c`。
2. 如果要让 JSON 成为真正的单一数据源，需要增加一个代码生成步骤，从 JSON 生成 LVGL 控件定义或布局表。
3. 屏幕较小，主提示建议控制在 3 到 4 行，按钮文字使用短英文可以避免字体和排版问题。
4. 新增中文文案前要确认字体字符覆盖，否则会显示缺字方框。
