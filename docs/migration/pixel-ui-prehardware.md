# Pixel UI migration and pre-hardware validation

## Reversible baseline
- Previous repository: f2582ef, tag backup/pre-ui-migration-20261017.
- Development branch: codex/ui-hardware-prep. No GitHub upload requested.
- Previous kernel and data image: /home/rbbird/focusmate-backups/pre-ui-20261017/.
- Existing ai_agent local patch backed up there, unchanged by migration.
- New default application: focusmate. Old implementation: focusmate --legacy (restart emulator before switching implementations).
- Pixel persistence uses /data/focusmate/pixel-v1.json, leaving legacy session/history files untouched.

## UI contract
- Standby -> touch enter or simulated wake -> greeting with Today/Add.
- Add -> Talk press -> recording -> second Talk press -> title confirmation -> confirm/retry. Confirmation returns to Add and never starts a timer.
- Today -> unfinished task -> 15/25/45 minutes -> start intent -> wait for phone -> focus.
- Phone detection must be stable for 650ms. Pickup pauses; replacement resumes only a phone-caused pause.
- Manual pause, Home, cancelled waiting and completed rounds cannot be restarted by phone placement.
- Home keeps active work; Today restores paused work or pending completion review.
- Timer end does not complete a task. Completed -> summary -> task list; Continue -> duration selection.
- Difficulty advice and task-title filler removal are local prototype logic, not LLM inference.

## Simulator controls
Use a wide emulator skin (900x600): left is the 390x450 device canvas; right is a scrollable lab panel and does not belong on the hardware screen.
Lab: Home, Talk, wake, phone placement, sound/silence, speech sample, fast-forward, timeout, network status, battery/charging and real/mock voice switch.
CLI commands shown on startup reach the same flow controller. speech <text> changes the mock utterance, note <text> changes recovery note.

## Hardware preparation boundaries
1. Phone: stable-event/debounce/ownership semantics are implemented. A future GPIO adapter should feed flow_phone(); do not modify flow state directly. No physical pin assigned or verified.
2. Audio: manual record endpoint and silence retry implemented. Real voice uses existing voice_channel_start/stop_with_text when compiled; ASR credentials/device and real capture still need verification. Mock waveform is explicitly synthetic; real waveform amplitude is not implemented. pcm <path> probes signed 16-bit mono PCM, 16kHz contract; it does not perform ASR or playback. Speaker playback remains an adapter integration task; no audio-output success claimed.
3. Network: lab injects online/offline status; existing local tasks/timer remain available. This is not Bluetooth PAN or an Internet connection. Target network transport, timeout/reconnect and credentials remain to be implemented after stack audit.
4. Power: default USB / battery unknown; low battery and charging states are simulated. No fabricated real percentage; charging/current/ADC and brownout persistence need hardware verification.
5. UI: native LVGL code, not an embedded HTML browser. Physical key bit mapping must be verified before enabling on board; lab buttons provide simulator input.

## Verification
Host C regression: gcc -std=c99 -Wall -Wextra -I app/focusmate tests/test_focus_flow.c app/focusmate/core/focus_flow.c -o /tmp/test-focus-flow && /tmp/test-focus-flow
Record build/runtime/visual results separately in docs/evidence. Do not treat the host state-machine test as LVGL, audio or hardware validation.

## Additional integration entry points
- `asr <pcm-file>` delegates file recognition to the existing ai_agent voice test API (when compiled); `speak <text>` delegates TTS/playback to its existing voice backend. They are explicit developer commands, not verified hardware capabilities. They may call configured cloud services; no credentials are added by this migration.
- `pcm <path>` is only the local PCM-format/peak probe. Do not confuse it with the `asr` command.
- The default launcher disables audio. To test actual host audio, run emulator manually without `-no-audio`; audio device/backend configuration still needs inspection.
- `tools/run_focusmate_pixel.sh` stores runtime data in ~/.local/share/focusmate-pixel/data.bin to avoid rebuilds regenerating the user's data image.
- Verified Home/Talk bit masks are Kconfig settings defaulting to 0 (disabled). No board-specific bit mapping is guessed.
- The new application releases its own LVGL display/input resources on quit; this fixes the repeated-start fdcheck assertion found during migration. Re-test result is recorded in evidence.
