# FocusMate 使用手册

> openvela 竞赛作品 · 队伍 `contest2026_075_OpenNexus` · 分支 `dev-ai-contest-2026`

---

## 0. 这个设备是做什么的

FocusMate 是一个**主动式**桌面专注伙伴。和普通番茄钟最大的区别是：
**它不等你操作，而是自己感知并主动行动。**

把手机放在设备上 → 一键开始专注 → **拿起手机时设备自动暂停并保存进度**
→ 手机放回后**主动提示你继续**。整个过程你一句话都不用说。

---

## 1. 硬件与连接

| 项目 | 说明 |
|---|---|
| 开发板 | SF32LB52-DevKit-LCD（Cortex-M33） |
| 屏幕 | 1.85" AMOLED 390×450，**仅显示、无触摸层** |
| 物理按键 | **KEY2**（板上标 KEY2 / USR，引脚 PA11） |
| 电脑连接 | **UART 口**（CH343 芯片）→ Windows 显示为 `COM7` |
| 串口参数 | 1000000 8N1 |

> ⚠️ **板子上有两个 Type-C 口，务必用 UART 那个**。
> 另一个是芯片内置 USB CDC（只在上电运行固件后才出现），**不能用来烧录**。

---

## 2. 快速开始（3 步）

### 第 1 步 · 连接板子

把 USB 线插到板子的 **UART 口**，确认 Windows 设备管理器出现 `COM7`。

### 第 2 步 · 烧录固件

固件已经在虚拟机里编译好了：`~/openvela-workspace/cmake_out/sf32lb52_devkit_lcd_nsh/nuttx.bin`

在 Windows 上用 sftool 烧录（脚本已备好）：

```powershell
# 固件需先从 VM 取回（若本地没有）
scp -i codex_vm_ed25519 luo@192.168.152.128:~/openvela-workspace/cmake_out/sf32lb52_devkit_lcd_nsh/nuttx.bin $env:TEMP\nuttx.bin

# 烧录（--before default_reset 会自动复位进 BootROM，实测可靠）
powershell -File flash_board.ps1 -Bin $env:TEMP\nuttx.bin
```

看到 `FLASH OK` 即成功，板子会自动重启。

> 若提示下载失败，脚本会自动降级重试；仍失败则手动进 BootROM：
> 拔 USB → 按住板上 Reset → 插 USB → 松开 Reset，然后加 `-Before no_reset` 重跑。

### 第 3 步 · 运行

串口已经有一个交互脚本：

```powershell
# 只监听（看板子输出）
powershell -File nsh.ps1

# 发送命令
powershell -File nsh.ps1 -Commands 'focusmate' -AfterSeconds 6
```

---

## 3. 日常操作：只用按键（最常用）

**这是给评委演示时的主要交互方式**，全程不需要键盘。

| 操作 | 效果 |
|---|---|
| **短按 KEY2** | 执行当前状态的"主要动作" |
| **长按 KEY2（> 1.5 秒）** | 放弃当前专注（回到待机） |

**短按的含义随状态自动变化**，和屏幕左下角按钮完全一致：

| 屏幕显示的状态 | 短按 = |
|---|---|
| `IDLE`（待机） | **立即开始一个 25 分钟专注**（自动规划 3 个阶段） |
| `FOCUSING`（专注中） | 暂停 |
| `PAUSED`（已暂停） | 继续 |
| `INTERRUPTED`（手机被取走） | 继续 |
| `RECOVERING`（手机放回） | 继续 |
| `COMPLETED`（已完成） | 再来一次 |

---

## 4. 屏幕怎么看

```
        FocusMate            ← 标题 32px
        FOCUSING             ← 当前状态（颜色随状态变化）
          09:52              ← 大号倒计时 40px，每秒刷新
      ▓▓▓▓▓░░░░░░░           ← 当前阶段进度条
   1/3  整理思路             ← 第几阶段 / 阶段名
       51%
  已专注 03:20  中断 1 次     ← 累计时长与被打断次数
   [ PAUSE ]    [ STOP ]     ← 操作按钮（同时对应按键）
```

状态颜色：待机灰 · 规划琥珀 · 就绪绿 · 专注蓝 · 暂停橙 · **中断红** · 恢复天蓝 · 完成青

---

## 5. 完整演示流程（推荐按此顺序）

这段流程把作品的"主动 + 执行"讲完整，**全程只按一个键**：

1. **待机** — 屏幕显示「把手机放好 / 想好要做的事 / 点一下就好」
2. **短按 KEY2** — 直接开始 25 分钟专注，屏幕出现大号倒计时
3. **（把手机从设备上拿走）** — 屏幕立刻变红：`INTERRUPTED`
   「手机被取走 / 专注已停 / 中断 1 次」
   *注意：这一步不需要按任何键，设备自己发现的*
4. **（把手机放回去）** — 屏幕变天蓝：`RECOVERING`
   「你回来了!手机已放回 / 之前在做: 整理思路 / 点一下接着做」
5. **短按 KEY2** — 从原阶段原进度继续专注
6. **长按 KEY2** — 放弃，回到待机

> 第 3、4 步是**作品的核心**：设备主动感知、主动暂停、主动询问，
> 用户全程没有下过任何指令。

---

## 6. 进阶：串口命令

需要精确控制时，在 `focusmate>` 提示符下用命令：

| 命令 | 作用 |
|---|---|
| `goal <文字> <分钟>` | 设定目标与时长，触发规划 |
| `start` / `pause` / `resume` | 开始 / 暂停 / 继续 |
| `removed` / `returned` | 模拟"手机被取走 / 放回"（演示用） |
| `stage_done` | 模拟当前阶段结束 |
| `cancel` | 放弃本次专注 |
| `status` | 打印当前完整状态 |
| `demo` | **自动跑完整主动场景**（无需手动操作） |
| `save` / `restore` | 手动保存 / 恢复会话 |
| `help` / `quit` | 帮助 / 退出 |

**想快速看整条主动链路，直接用 `demo`**：

```
nsh.ps1 -Commands 'focusmate;;demo' -AfterSeconds 15
```

---

## 7. 用 AI 继续开发（日志会自动记录）

比赛要求提交 AI Coding 日志，**必须用官方支持的 4 种工具之一，且在 openvela 工作区内**。
环境已经全部搭好（OpenCode + DeepSeek）：

```bash
# SSH 进虚拟机后
cd ~/openvela-workspace/contest2026_075_OpenNexus     # ① 必须在 demo 仓库内
export PATH=/usr/local/bin:$PATH
script -qec "opencode run --model deepseek/deepseek-flash '你的开发任务'" /dev/null   # ② 需要伪终端
```

会话说结束后，日志**自动写入** `logs/tofreemyself/<日期>/opencode__<会话id>.jsonl`。

提交前检查：

```bash
# 官方校验器
python3 ../.claude/skills/contest-log-collector/tools/validate-log.py logs/

# 若报 duplicate seq（插件会写两遍，是已知 bug）：
python3 tools/dedupe_logs.py logs
```

然后正常提交：

```bash
git add logs/ && git commit -s -m "logs: capture session" && git push
```

> **两个必须遵守的条件**：
> ① 必须 `cd` 进 demo 仓库目录再启动（插件靠向上找 `.repo/` 判断是否在工作区）
> ② 必须用 `script` 包一层（`opencode run` 在无 TTY 时静默无输出）

---

## 8. 常见问题

| 现象 | 原因与处理 |
|---|---|
| `COM7` 不存在 | UART 线没插好，或插到了另一个 USB 口 |
| 烧录报 `Failed to download stub` | 板子没进 BootROM；拔插 USB 并按住 Reset 后再试 |
| 串口没输出 | 串口工具的 RTS 被拉低会把板子按在复位态，需保持 RTS 高电平 |
| 屏幕黑 | 确认用的是最新固件（≥ 2.2MB）并已重新上电 |
| 屏幕有内容但按键无反应 | 先短按一次唤醒；若在 `PLANNING` 状态请稍候 |
| 日志目录为空 | 检查是否满足第 7 节的两个条件；再跑 `validate-log.py` |

---

## 9. 关键文件速查

| 用途 | 路径 |
|---|---|
| 板级固件 | `cmake_out/sf32lb52_devkit_lcd_nsh/nuttx.bin` |
| 应用源码 | `app/focusmate/`（`core/` 状态机 · `ui/` 界面 · `agent/` 规划 · `sensor/` 感知 · `storage/` 持久化） |
| 字体重新生成 | `docs/board_bringup/gen_cjk_font.sh` |
| 日志去重工具 | `tools/dedupe_logs.py` |
| 烧录脚本 | Windows 侧 `flash_board.ps1` |
| 串口脚本 | Windows 侧 `nsh.ps1` |
