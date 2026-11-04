# FocusMate 微动开关与计时联动验收

日期：2026-11-02。当前状态：新版固件已编译、烧录并校验；第一步真实开关输入、第二步计时联动及手动暂停/Home 补测均通过。

实测设备：SF32LB52-DevKit-LCD V1.2.1，原配 QSPI AMOLED，USB UART 接 Windows COM5。用户操作实物，串口记录 GPIO 与页面变化；本轮没有使用 phone-in / phone-out 模拟输入。

## 为什么需要重新烧录

2026-10-24 保存的固件尚未将真实手机检测 GPIO 接入像素 UI。旧 phone_sensor.c 是模拟实现，UI 通过 phone-in / phone-out 等模拟命令更新状态。接好导线不会自动改变软件。

本轮已实现并编入：GPIO 输入上拉、低电平表示按下的转换、650 ms 稳定确认、GPIO 设备错误处理，以及对现有计时状态机的输入。重新编译并烧录后才完成以下实板验证。今后只更换相同接法的开关或导线，通常不需要重复烧录；修改 GPIO 引脚或程序时才需要重新编译、烧录。

## 接线与配置复核

- 开关使用 COM 和 NO。此次实际接线已通过按下、松开各 3 次验证；不按照片猜三脚顺序。
- 使用扩展排针 5（PA41）和 6（GND）；编号来自厂家 40P 定义图，不是芯片管脚编号。板子正面朝上、天线端在上、USB 在下时，从 USB 端数第三对：内列 5 为 PA41，外列 6 为 GND。
- 本轮先断开 PA41 信号线，烧录后通过 `gpio /dev/phone0` 确认输入上拉、松开为高电平，再重新连接开关。
- 不将开关接在电源与 GND 之间；不对未知状态的输出 GPIO 直接接地。
- USB UART 接 Windows 进行烧录；Ubuntu 用于编译，串口不同时被 Windows 与虚拟机占用。

## 第一步：真实输入

| 操作 | 结果 | 依据 |
|---|---|---|
| 松开开关 | 通过：raw=1，present=0 | 串口原始电平与稳定状态 |
| 按下开关 | 通过：raw=0，present=1 | 串口原始电平与稳定状态 |
| 按下、松开各 3 次 | 通过：每次状态一致 | 连续日志，用户确认完成操作 |
| 短暂接触变化 | 状态机测试通过；尚未用仪器测量实际触点抖动 | 300 ms 变化不进入专注，650 ms 稳定后才更新 |
| GPIO 设备打开或读取失败 | 已实现错误提示及暂停；状态机测试通过 | 未在实板注入驱动错误 |

## 第二步：计时与 UI

| 场景 | 结果与依据 |
|---|---|
| 选好任务和时长，开关松开，点击开始 | 实板通过：停在 wait-phone，剩余 1500 秒 |
| 等待时稳定按下开关 | 实板通过：running 从 1500 秒开始 |
| 专注时稳定松开 | 实板通过：paused，剩余 1490 秒 |
| 手机导致暂停后重新按下 | 实板通过：从 1490 秒恢复，未重置；再次松开暂停在 1482 秒 |
| 用户手动暂停，松开再按下 | 实板通过：日志仍为 paused，剩余 2695 秒；用户确认符合 |
| 按 KEY2 Home 后松开再按下 | 用户实板确认通过；本次日志另记录到 KEY2 的 0x1 事件及 standby，但采集结束前未完整覆盖最后一组切换 |
| 初始状态已经按下、重复采样 | 自动测试通过：同一状态每 50 ms 采样，650 ms 后正确生效；本轮未另做按住开关重启实板 |
| 一轮结束后改变开关 | 自动测试通过：需用户确认任务是否完成；实板另观察到 end → review → summary → list |

用户分别确认了“等待→倒计时→暂停→继续倒计时→暂停”全部符合，以及“手动暂停、Home 两项都符合”。

## 实现与复现

- 板级：`app/focusmate/sensor/sf32lb52_phone_gpio.c` 将 PA41 设为输入上拉，注册只读 `/dev/phone0`。未使用原有的 gpio0～gpio2 作为替代。
- 应用：`sensor/phone_gpio.c` 用 openvela 的 `open()` / `ioctl(GPIOC_READ)` 读取，低电平转换为已放置；UI 约每 50～60 ms 采样。
- 状态机：`core/focus_flow.c` 做 650 ms 稳定确认。只有手机移开导致的暂停允许自动恢复，手动暂停和 Home 不受开关恢复事件影响。
- 修复：此前重复输入会持续重置首次消抖计时，现已修正。驱动打开/读取失败时设为未知并暂停，恢复后需要手动继续。
- 真实 GPIO 固件拒绝 `phone-in` / `phone-out` 模拟事件，避免把模拟结果当成实板结果。
- 板级增量补丁：`patches/board/phone-gpio-pa41.patch`；基于已有可运行的板级配置应用，不能把它当成从原厂干净目录开始的全部移植补丁。

Ubuntu 已有工程中使用的构建方式：

```sh
cd $OPENVELA_ROOT
cmake -B cmake_out/sf32lb52_devkit_lcd -S nuttx -DFOCUSMATE_PHONE_GPIO=ON
cmake --build cmake_out/sf32lb52_devkit_lcd -j4
```

需先保留原工程的工具链 PATH 和板级配置。`FOCUSMATE_PHONE_GPIO=ON` 是本次实际使用的 CMake 开关；自动生成的 `.config` 不一定出现同名 Kconfig 项。仅验证了此板的 QSPI 屏幕配置，切换 DBI 屏时 PA41 可能被占用，需要重新评估引脚。

本轮状态机测试已执行通过：

```sh
gcc -Wall -Wextra -Werror -Iapp/focusmate tests/test_focus_flow.c app/focusmate/core/focus_flow.c -o /tmp/focusmate-phone-flow-test
/tmp/focusmate-phone-flow-test
```

构建通过，`git diff --check` 通过。固件文件大小 2,492,420 字节。烧录地址 `0x12010000`，sftool 使用 `write_flash --verify`，退出码 0。

## 证据与回退

- Windows 本次产物：`output/firmware/2026-11-02/`，包含 `focusmate-sf32lb52-phone.bin`、烧录日志、启动日志及 `phone-live-windows.log`。
- 新固件 SHA256：`ca715b3f932fba9784528f925c45a9aa48ebecf6ccf16dfcfbb24a17bb311dc0`。
- 上一版本保留于 `output/firmware/2026-10-24/focusmate-sf32lb52-serial-agent.bin`，SHA256：`5bf0017e86f03028a12e8896bd57675d832f7d83d763d4cccfe1ebc2cae39dbe`。需要回退时，使用同一烧录工具、同一地址及校验选项重新写入该文件。
- Ubuntu 修改前备份：`$HOME/focusmate-backups/pre-phone-20261102/`，保留旧固件、配置、板级原文件及修改前差异。
- 源代码在 Ubuntu 工程本地保存；本轮不上传 GitHub。

## 仍未完成

1. 把开关固定进手机托架，实际验证不同手机放置能稳定压合、拿起能释放。当前证明的是开关通断与程序联动，不能识别压上去的物体是否为手机。
2. 板载麦克风实际录音、回放及识别链路；此前扬声器提示音已通过，不能据此认定收音正常。
3. iPhone 蓝牙中转联网及客户端后台运行；本次仍是 USB 供电，不代表 BLE 已跑通。
4. 电池独立供电、充电、电量提示、低电量处理，以及掉电保存和恢复。
5. Agent 主动提醒、手机端目标拆解/每日任务同步等后续业务。

输入上拉只能把开关断开读为“未放置”；信号线意外断线也可能表现相同，不能据此自动判断接线故障。
