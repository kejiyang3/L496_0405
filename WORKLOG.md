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

- Workspace: `C:\Users\ycs\OneDrive\妗岄潰\codexppt`
- Main project folder: `C:\Users\ycs\OneDrive\妗岄潰\codexppt\L496_0405`
- Archive present: `C:\Users\ycs\OneDrive\妗岄潰\codexppt\L496_0405.rar`

## Status

Four-modal recording (ECG+PPG+IMU+MIC) is stable. Interrupt-driven PPG/IMU confirmed. 30s auto-stop default. sd_debug_tool v2 provides reliable USB CDC SD access for closed-loop validation. Documented in four Markdown files.

## Closed-Loop Development Process

```
 1. 淇敼浠ｇ爜 鈫?2. ninja 鏋勫缓 鈫?3. OpenOCD 鐑у綍涓诲浐浠?
 4. 绛夊緟璁惧鑷姩褰曞埗锛堝綋鍓?30s锛?
 5. 鍙€夛細OpenOCD 璇诲唴瀛?瀵勫瓨鍣ㄥ仛鍦ㄧ嚎楠岃瘉
 6. OpenOCD 鐑у綍 sd_debug_tool
 7. python usb_sd_pull.py get-all 鎷夊彇鍏ㄩ儴 SD 鏂囦欢
 8. 鍒嗘瀽 session/log/CSV/WAV 鈫?鎬荤粨鐜拌薄
 9. 鏇存柊 WORKLOG.md
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
1. SD artifacts pulled via sd_debug_tool (trusted 鈥?ACK protocol)
2. OpenOCD memory/register reads
3. USB CDC logs (weak 鈥?may drop/truncate)

## Completed

- Project Markdown consolidated to four files.
- Hardware interrupt mapping established: ECG_INT=PB6/EXTI9_5, ICM_INT=PH1/EXTI1, PPG_INT=PC2/EXTI2.
- Interrupt-driven IMU/PPG recording implemented (replaced polling).
- Four-modal recording stabilized with fail-fast policy, readiness gates, I2C3 recovery, audio reopen protection.
- RECORD_DEFAULT_RECORD_MS set to 30s; 10-minute auto-stop also hardware-verified.
- LVGL STOP button hardened (latched touch IRQ, stop-on-press).
- Audio signed 24-bit 鈫?PCM16 conversion fixed; DMA half accounting added.
- SD-over-CDC browsing removed; recorder focuses on four-modal SD persistence.
- sd_debug_tool v2 integrated: ACK+CRC, resume, progress, get-all. Proven in multiple closed-loop runs.
- Closed-loop process formalized and documented.
- Multiple validation runs: short (30s) and long (10min+) 鈥?all modalities verified via SD artifacts.
- Known issue: ECG effective rate ~244 Hz vs configured 512 SPS. Root cause under investigation.

## In Progress

- Keeping this worklog as the cross-session recovery source.

## ECG Four-Modal Closed Loop (2026-05-28)

Goal: preserve four-modal recording while resolving ECG under-rate. If 512 Hz could not be made stable, reduce MIC to 8 kHz, then reduce ECG sampling.

Final accepted baseline:
- `RECORD_MIC_SAMPLE_RATE_HZ=8000U`.
- `RECORD_ECG_SAMPLE_RATE_HZ=128U`.
- MAX30003 uses 256-sps register mode to produce about 126 Hz effective persisted ECG on current hardware.
- MAX30003 FCLK path strengthened with high LSE drive and very-high-speed PA8 MCO.

Final hardware evidence pulled by sd_debug_tool:
- Path: `L496_0405\sd_pull_seq001_final`
- Validator: `PASS seq=001 duration_ms=30004`
- CSV counts: ECG `3770`, PPG `378`, IMU `1566`
- Spans: ECG `29996 ms`, PPG `29985 ms`, IMU `29984 ms`
- MIC duration: about `30.25 s`
- Session counters: all ECG/PPG/IMU write failures and block drops were 0; MIC drops and write errors were 0.

Note: MAX30003 still reports PLLINT/eovf counters. Future hardware/FCLK investigation can target that, but the requested fallback path now achieves verified four-modal SD capture.

## Key Files

- `README.md`: system-level project map and baseline.
- `DEBUG_GUIDE.md`: build/flash/debug workflow and hardware diagnostics.
- `FEATURE_NOTES.md`: archived feature notes, overnight findings, UI plan, future route.
- `WORKLOG.md`: this file 鈥?decisions, status, closed-loop process.
- `L496_0405\.em_skill.json`: embed-ai-tool project profile.
- `L496_0405\Core\Src\freertos.c`: task creation and recording state machine.
- `L496_0405\Core\Src\multi_sensor_logger.c`: ECG/PPG/IMU CSV block writer.
- `L496_0405\Core\Src\audio_recorder.c`: microphone WAV recording.
- `L496_0405\Core\Src\sd_debug_log.c`: session summary and SD debug evidence.

## Decisions

- Use local files as source of truth for handoff, not remote compact.
- Treat `README.md` as the first-read project map.
- USB CDC is weak evidence; SD artifacts and counters are trusted. sd_debug_tool ACK protocol eliminates CDC unreliability for file pulls.
- Closed-loop: read docs 鈫?inspect source 鈫?scoped change 鈫?build 鈫?flash 鈫?run 鈫?sd_debug_tool pull 鈫?analyze 鈫?update WORKLOG.
- Current hardware truth: ECG_INT=PB6/EXTI9_5, ICM_INT=PH1/EXTI1, PPG_INT=PC2/EXTI2.
- IMU recording is interrupt-driven; polling must not be reintroduced.
- PPG uses INT-driven FIFO drain as primary trigger; timeout drain is stuck-low fallback only.
- Fail-fast is ON: any modality runtime failure stops the session.
- 30s auto-record is primary validation path; touch STOP is fallback.
- Four-modal baseline must be preserved; do not silently degrade to ECG-only.

## Handoff Prompt

Paste this into a new Codex conversation when the current one gets long:

```text
璇疯鍙?C:\Users\ycs\OneDrive\妗岄潰\codexppt\WORKLOG.md锛岀户缁笂娆′换鍔°€?
瑕佹眰锛?
1. 鍏堟鏌ュ綋鍓嶉」鐩姸鎬佸拰 WORKLOG.md锛屼笉瑕佷緷璧栨棫瀵硅瘽璁板繂銆?
2. 涓嶈鍥炴粴浠讳綍鐢ㄦ埛宸叉湁鏀瑰姩锛岄櫎闈炴垜鏄庣‘瑕佹眰銆?
3. 鎸?WORKLOG.md 閲岀殑 Current Goal銆丼tatus銆丯ext Steps 缁х画銆?
4. 姣忓畬鎴愪竴涓樁娈甸兘鏇存柊 WORKLOG.md銆?
5. 濡傛灉涓婁笅鏂囦娇鐢ㄦ帴杩?60%锛岃涓诲姩鏁寸悊鏂扮殑 Handoff Prompt锛屽苟鎻愰啋鎴戝紑鏂板璇濄€?
```

