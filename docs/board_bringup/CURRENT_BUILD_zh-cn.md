# 当前实板配置与复现（2026-11-04）

目标：SF32LB52-DevKit-LCD V1.2.1 MOD-1，390×450 CO5300 AMOLED。代码在 Ubuntu openvela 工作区编译，Windows USB UART 用于当前实板验收。路径示例以 `$OPENVELA_ROOT` 表示工作区根目录。

## 补丁约定

`patches/board/current-sifli.patch`、`current-nuttx.patch` 是当前工作区相对于各依赖仓库基线的**累计补丁**，包含此前按键、触摸、GPIO、PCM、HCI、共享内存边界和启动缓存配置所需修改。依赖提交号及调度器源文件哈希见同目录 `current-bases.json`。

**新工作区应用累计补丁即可，不要再逐个叠加历史 board/audio/mic/heap 补丁。** 历史细分补丁用于追溯。应用工具首先做正向和反向检查；若已经部分应用而无法判断，将停止，避免强行覆盖。

```bash
cd "$OPENVELA_ROOT/contest2026_075_OpenNexus"
./tools/apply_current_board_patches.sh "$OPENVELA_ROOT"
```

原始工作区的 `ai_agent` 另有既存适配，已记录于 `current-ai-agent.patch`；为重现旧串口 Agent/语音链路时才应用该补丁。当前原生提醒诊断不等于完整 ai_agent 固件。不要修改或上传本地模型凭证。

## 构建当前诊断配置

先按原有 `build.sh` 流程建立 SF32LB52 CMake 构建目录，确认作品仓的 manifest linkfile/symlink 已生效，再执行：

```bash
cd "$OPENVELA_ROOT"
python3 contest2026_075_OpenNexus/tools/hardware/prepare_sf32_audio.py "$OPENVELA_ROOT"
python3 contest2026_075_OpenNexus/tools/hardware/prepare_sf32_ble.py "$OPENVELA_ROOT" --cache-disabled-at-boot
cmake -S nuttx -B cmake_out/sf32lb52_devkit_lcd \
  -DFOCUSMATE_PHONE_GPIO=ON -DFOCUSMATE_BOARD_AUDIO=ON \
  -DFOCUSMATE_BLE_RELAY=ON -DFOCUSMATE_REMINDER=ON \
  -DFOCUSMATE_MIC_TEST=OFF
cmake --build cmake_out/sf32lb52_devkit_lcd -j6
```

使用工作区内适合宿主架构的 `arm-none-eabi` 工具链和 Kconfig 工具。检查生成的 `include/nuttx/config.h`：`CONFIG_ARMV8M_DCACHE`/`CONFIG_ARCH_DCACHE` 不应启用，`CONFIG_ARMV8M_ICACHE` 保留。

`SF32LB52_DCACHE_BOOT_DISABLED` 默认关闭；只有明确选择诊断配置时才关闭数据缓存。此配置以正确通信为验证目标，有性能代价，不是最终功耗/性能方案。

## 设备命令

```text
focusmate_ble start
focusmate_ble status
focusmate_ble heap
focusmate_remind after 60 "写报告"
focusmate_remind list
focusmate_remind cancel <实际id>
```

广播尚未自动启动；当前配置不需要执行运行中缓存切换。保留的 `nocache` 命令只报告启动状态；若使用启用缓存的构建，它会拒绝危险切换。原始 `probe` 的关闭路径仍有历史异常，当前正常验证不运行它。

KEY2 为 Home，KEY1 为短按切换录音。手机开关接 PA41 与 GND，输入上拉、低有效；具体排针定位以对应板型的引脚图为准。

## 测试与回退

```bash
./tools/run_host_checks.sh "$OPENVELA_ROOT"
node --test tools/phone-relay/test_protocol.mjs
```

主机 cron 测试使用受控时钟；实板约 60 秒日志和用户触摸确认是另外的证据。手机网关协议测试通过不等于 iPhone HTTPS 成功。

保留已验收固件的本地回退副本；回退源码时使用原分支或本轮本地检查点，并恢复匹配的依赖补丁和配置。当前 `/data` 为内存盘，回退与复位不能保证任务保留。完整构建配置、源版本、固件哈希和实板日志须配套记录。
