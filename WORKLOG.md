# Codex Worklog

Purpose: keep the project recoverable when a Codex conversation gets long or compact fails.

## Current Goal

- Keep the `L496_0405` STM32L496 recorder firmware recoverable across Codex sessions, with project-owned Markdown consolidated to four files.

## Working Rules

- Update this file after every meaningful stage of work.
- Keep the notes short, factual, and useful for a fresh Codex session.
- Do not rely on chat history for important decisions. Put decisions here.
- When context usage reaches about 60%, prepare a handoff prompt and suggest starting a new conversation.
- Never revert user changes unless the user explicitly asks for that.

## Project Map

- Workspace: `C:\Users\ycs\OneDrive\桌面\codexppt`
- Main project folder: `C:\Users\ycs\OneDrive\桌面\codexppt\L496_0405`
- Archive present: `C:\Users\ycs\OneDrive\桌面\codexppt\L496_0405.rar`

## Status

Four-modal recording (ECG+PPG+IMU+MIC) is stable. Interrupt-driven PPG/IMU confirmed. 30s auto-stop default. sd_debug_tool v2 provides reliable USB CDC SD access for closed-loop validation. Documented in four Markdown files.

## Closed-Loop Development Process

```
 1. 修改代码 → 2. ninja 构建 → 3. OpenOCD 烧录主固件
 4. 等待设备自动录制（当前 30s）
 5. 可选：OpenOCD 读内存/寄存器做在线验证
 6. OpenOCD 烧录 sd_debug_tool
 7. python usb_sd_pull.py get-all 拉取全部 SD 文件
 8. 分析 session/log/CSV/WAV → 总结现象
 9. 更新 WORKLOG.md
```

**Key commands:**

```powershell
# Build & flash main firmware
cd L496_0405 && ninja -C build
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program build/L496_0405.elf verify reset exit"

# Online memory check (optional)
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "init" -c "halt" -c "mdw 0x20000180 16" -c "resume" -c "shutdown"

# Flash SD debug tool & pull files
cd sd_debug_tool
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program sd_debug.hex verify reset exit"
python pc\usb_sd_pull.py --port COM12 get-all -o ..\sd_pull_output
```

**Evidence priority (strongest first):**
1. SD artifacts pulled via sd_debug_tool (trusted — ACK protocol)
2. OpenOCD memory/register reads
3. USB CDC logs (weak — may drop/truncate)

## Completed

- Project Markdown consolidated to four files.
- Hardware interrupt mapping established: ECG_INT=PB6/EXTI9_5, ICM_INT=PH1/EXTI1, PPG_INT=PC2/EXTI2.
- Interrupt-driven IMU/PPG recording implemented (replaced polling).
- Four-modal recording stabilized with fail-fast policy, readiness gates, I2C3 recovery, audio reopen protection.
- RECORD_DEFAULT_RECORD_MS set to 30s; 10-minute auto-stop also hardware-verified.
- LVGL STOP button hardened (latched touch IRQ, stop-on-press).
- Audio signed 24-bit → PCM16 conversion fixed; DMA half accounting added.
- SD-over-CDC browsing removed; recorder focuses on four-modal SD persistence.
- sd_debug_tool v2 integrated: ACK+CRC, resume, progress, get-all. Proven in multiple closed-loop runs.
- Closed-loop process formalized and documented.
- Multiple validation runs: short (30s) and long (10min+) — all modalities verified via SD artifacts.
- Known issue: ECG effective rate ~244 Hz vs configured 512 SPS. Root cause under investigation.

## In Progress

- Keeping this worklog as the cross-session recovery source.

## Key Files

- `README.md`: system-level project map and baseline.
- `DEBUG_GUIDE.md`: build/flash/debug workflow and hardware diagnostics.
- `FEATURE_NOTES.md`: archived feature notes, overnight findings, UI plan, future route.
- `WORKLOG.md`: this file — decisions, status, closed-loop process.
- `L496_0405\.em_skill.json`: embed-ai-tool project profile.
- `L496_0405\Core\Src\freertos.c`: task creation and recording state machine.
- `L496_0405\Core\Src\multi_sensor_logger.c`: ECG/PPG/IMU CSV block writer.
- `L496_0405\Core\Src\audio_recorder.c`: microphone WAV recording.
- `L496_0405\Core\Src\sd_debug_log.c`: session summary and SD debug evidence.

## Decisions

- Use local files as source of truth for handoff, not remote compact.
- Treat `README.md` as the first-read project map.
- USB CDC is weak evidence; SD artifacts and counters are trusted. sd_debug_tool ACK protocol eliminates CDC unreliability for file pulls.
- Closed-loop: read docs → inspect source → scoped change → build → flash → run → sd_debug_tool pull → analyze → update WORKLOG.
- Current hardware truth: ECG_INT=PB6/EXTI9_5, ICM_INT=PH1/EXTI1, PPG_INT=PC2/EXTI2.
- IMU recording is interrupt-driven; polling must not be reintroduced.
- PPG uses INT-driven FIFO drain as primary trigger; timeout drain is stuck-low fallback only.
- Fail-fast is ON: any modality runtime failure stops the session.
- 30s auto-record is primary validation path; touch STOP is fallback.
- Four-modal baseline must be preserved; do not silently degrade to ECG-only.

## Handoff Prompt

Paste this into a new Codex conversation when the current one gets long:

```text
请读取 C:\Users\ycs\OneDrive\桌面\codexppt\WORKLOG.md，继续上次任务。
要求：
1. 先检查当前项目状态和 WORKLOG.md，不要依赖旧对话记忆。
2. 不要回滚任何用户已有改动，除非我明确要求。
3. 按 WORKLOG.md 里的 Current Goal、Status、Next Steps 继续。
4. 每完成一个阶段都更新 WORKLOG.md。
5. 如果上下文使用接近 60%，请主动整理新的 Handoff Prompt，并提醒我开新对话。
```