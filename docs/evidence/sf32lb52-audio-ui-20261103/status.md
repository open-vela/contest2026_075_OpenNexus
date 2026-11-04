# FocusMate UI 真实录音与 openvela 音频接口

日期：2026-11-03。源代码基线：本地 `b63e94d`。本轮不上传 GitHub。

当前状态：修正版已成功烧录并校验，用户确认屏幕显示 FocusMate。连续六轮取消/Home 测试（12 次采集）通过，真实录音、手动结束、PCM 导出校验与软件回放均通过。用户确认实体 KEY1 两次短按及 KEY2 返回均正常。回放时扬声器尚未连接，用户随后明确选择暂不测外放，听感验收留待以后接入 SPK 后补测；不能将软件播放完成当成扬声器验收通过。

## 实现

- 新增 SF32LB52 音频 lower-half，注册 `/dev/audio/pcm0c`（采集）和 `/dev/audio/pcm0p`（播放），目前支持 16 kHz、单声道 PCM16，采集与播放依次使用。
- 板级驱动使用 4 KB 内部 SRAM 循环缓冲与 DMA1 CH8，中断唤醒工作线程，再把数据交给 openvela 音频队列。
- 应用通过 `open()`、`AUDIOIOC_CONFIGURE`、`AUDIOIOC_ALLOCBUFFER`、消息队列和 `AUDIOIOC_START/STOP` 使用录音设备，不再从 UI 直接调用芯片 HAL。
- 录音手动开始、手动结束，停顿不会自动结束。应用录音保存在内存，最多 60 秒；到达容量限制时明确提示。波形来自真实采样幅值。
- 原始录音暂未接入 ASR，所以结束后进入“录好了”页面，可回放、重新录音或返回添加，不会用模拟文本生成任务。
- KEY2 保留 Home；新增对 PA34/KEY1 的只读取样，保留默认下载/电源下拉。实体 KEY1 开始/结束录音、KEY2 返回开始页，用户已确认正常。

## 验证方式

本轮复用 Windows 已保存的 `voice-final.wav` 作为测试声源，不要求用户重复说话。测试音频及录音导出仅保存在本机，不纳入 Git。

本地调试入口：

```text
audio_probe record 2
audio_probe status
audio_probe play
focusmate_ctl add
focusmate_ctl talk
focusmate_ctl audio-status
focusmate_ctl audio-dump
focusmate_ctl home
```

`focusmate_ctl` 通过本地消息队列交给 UI 所在线程执行，用于自动化验证，避免其他线程直接操作 LVGL。

已执行通过的主机测试：`test_focus_flow`、`test_voice_flow`。后者检查原始录音不会冒充已识别任务，并检查重录、取消、Home 和迟到回调的导航行为。

已完成的试验：

- `/dev/audio/pcm0c`、`pcm0p` 已在实板注册，标准音频接口可采集和播放。
- UI 采集已保存的人声测试声源，手动停止后进入 `voice-ready`，任务数量保持 3 项，未生成模拟识别任务。
- 首次 UI 导出 PCM 为 110,592 B / 3.456 秒，FNV1a `57fd672e` 校验通过，无削波；同一段录音的板上回放返回 `result=0`。该结果说明播放链路完成，不等同于用户已确认声音清晰。
- 带诊断日志固件再次采集 227,328 B / 7.104 秒，`signal=1`、`error=0`；连续三轮取消和 Home 返回通过，结束后录音线程已退出。
- 本地 `test_focus_flow`、`test_voice_flow` 均以 `-Wall -Wextra -Werror` 编译运行通过。

修正版本次重试结果（证据在 `output/firmware/2026-11-03/audio-ui/`）：

- `fixed-flash-after-cold-replug.rc` 为 0，`write_flash --verify` 完成；写入后音频节点存在、FocusMate 任务运行、今日任务仍为 3 项，用户确认屏幕已亮。
- `fixed-ui-navigation.log`：6 轮取消/Home，12 次采集启动、12 次 `result=0` 停止，未出现非零错误；开始页状态和任务数量保持正确，结束后无遗留音频线程。
- DMA IRQ 优先级每次启动读回均为 `4`（CMSIS 格式）。之前文档预估的 `2` 不准确，现按实板读回记录。
- `fixed-ui-capture.log`：真实采集 221,184 B / 6.912 秒，`signal=1`、`error=0`，手动结束进入 `voice-ready`；任务数量未变化。
- `fixed-ui-capture.json`：110,592 个 PCM16 样本，FNV1a `00f55d05` 校验通过，峰值 1699，RMS 196.873，无削波。
- `fixed-ui-playback.log`：播放同一段录音，`bytes=221184 result=0`，正常返回录音完成页。随后用户说明扬声器未插，声学外放验收仍待补测。
- `fixed-physical-keys.log` 记录到实体 KEY1 启动；监听窗口未覆盖完整三个按键，不能称为完整日志验收。用户确认三次短按均正常，后续 `fixed-after-physical-keys.log` 确认为开始页、采集停止。
- 六轮测试后堆使用 2,715,568 B，空闲 6,038,376 B；保留了一份 1,920,000 B 录音缓冲。此结果不替代长时间运行或容量上限测试。

稳定性问题与修正：

- 初版连续测试中两次出现串口失去响应，分别发生在重复页面切换和取消录音阶段。不能将加日志后的通过当成已经修复。
- 当前使用的 SiFli HAL 在分配/启动 DMA 时会重写 NVIC 优先级。原先清零的 `dma.Init.IrqPrio` 导致音频 IRQ 为最高优先级，覆盖 openvela 默认值；修正版按 `NVIC_SYSH_PRIORITY_DEFAULT` 转换为 CMSIS 格式，实际读回为 `4`。该配置缺陷已修正，本次六轮测试未复现先前问题，尚不能据此证明所有长期运行场景均稳定。
- 停止时先关闭 ADC/DAC 请求，再释放循环 DMA；清理时先 RELEASE 驱动所有权，再释放客户端缓冲引用，并检查 HAL 未将固定 DMA 通道改配到其他通道。
- 实体按键后串口状态查询仍正常，随后 `help` 等命令未返回；用户确认屏幕可以操作。当前证据说明串口会话暂时失去响应，不能直接判定为整机死机。接扬声器重新上电后继续复查；当时发送的独立提示音命令也没有确认执行，不能计为提示音测试通过。

## 复现与回退

系统配置需启用 `CONFIG_AUDIO`、PCM 与驱动指定缓冲区，Home 掩码为 `0x1`，Talk 掩码为 `0x2`。CMake 开关：

```text
-DFOCUSMATE_PHONE_GPIO=ON -DFOCUSMATE_BOARD_AUDIO=ON -DFOCUSMATE_MIC_TEST=OFF
```

关闭旧 `mic_test` 入口可移除其 104 KB 诊断缓冲区。本次链接静态 SRAM 使用 142,856 B / 512 KB；应用录音的动态内存和运行时峰值不包含在此数值中。

已烧录修正版 `output/firmware/2026-11-03/audio-ui/focusmate-sf32lb52-audio-fixed.bin`，2,519,312 字节，Windows 与 Ubuntu SHA256 均为 `d5e7dca590c5f2dbfd09559ba6d3dd6930e128a203809d9864988945ae45d4da`。

旧试验版 `focusmate-sf32lb52-audio-ui.bin` 和带日志版 `focusmate-sf32lb52-audio-trace.bin` 均保留；各版本的证据文件分别保存。

修改前备份：Ubuntu `$HOME/focusmate-backups/pre-audio-ui-20261103/`，Windows 旧固件 `output/firmware/2026-11-03/focusmate-sf32lb52-mic.bin`。未执行整片擦除。

第一次烧录在辅助程序下载阶段超时；用户短按 RESET 后改用 `--before no_reset` 重试。以本目录的 `flash-retry-windows.rc` 和后续启动日志为准，不能把首次失败算作成功。

修正版此前取回文件时 SCP 连接中断，烧录工具因找不到文件退出，未写入；后续也曾在辅助程序下载阶段超时。本次普通重试、自动复位仍失败，用户重新插拔并按 RESET 后，NSH 与 Debug IP 握手恢复，随后写入和校验成功。以 `fixed-flash-after-cold-replug.rc`、`fixed-boot.log` 和本次修正版验证记录为准，保留此前失败日志。

本板本次 RTS true/false/true 的短时对照均能响应 NSH，因此不能认定 RTS 就是此前连接失败的根因。[官方文档](https://wiki.sifli.com/board/sf32lb52x/SF32LB52-DevKit-LCD.html)记录了部分开发板打开串口时可能复位/关机的情况，该信息仅用于排查，没有据此修改 Windows 驱动或系统配置。

本轮没有接入 ASR、语音唤醒、iPhone 蓝牙中转，也未验证电池供电。外放按用户要求暂缓，不再播放或录音。下次继续时先复查串口会话；外放具备接线条件后再补测可听声音，随后推进 iPhone BLE 可用性与 ASR 路径。
