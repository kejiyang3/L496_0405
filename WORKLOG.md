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


## BLE 状态机 + 闭环验证 (2026-05-30)

**BLE 状态机实现:**
- 新增 Core/Inc/ble_state_machine.h / Core/Src/ble_state_machine.c
- 支持 7 条命令: PING/START/STOP/STATUS/FNAME/INFO/SYNC
- StartTask_BLE 接入行解析 + BLE_SM_Send UART 响应通道
- 编译 0 错误 0 警告，Flash 402KB (78.5%)
- 已合入 master，推送 GitHub

**闭环验证 (20260530_210000):**
- 烧录: ✅ Verified OK (主固件 + sd_debug_tool)
- 录音时长: 94.2s (配置 90s)
- ECG: 35,187 samples @ ~374 Hz (73% of 512 Hz), 18 EOVF
- PPG: 1,140 samples @ ~12 Hz (FIFO averaging)
- IMU: 4,688 samples @ ~50 Hz (I2C3 sharing)
- MIC: 73,728 samples, 仅 9.2s 有效 — 🔴 掉块问题
- SD 写入错误: 0
- 详细报告: CLOSED_LOOP_20260530_DIAG_REPORT.md

**飞书同步:**
- 软件表 BLE通信: 未开始 → 进行中
- 变更记录表: 新增 BLE 状态机实现记录

**发现的问题:**
1. 🔴 音频 9.2s 后中断 (mic_drop_blocks=2, error_count=1)
2. ⚠️ ECG 512Hz → ~374Hz, 96% 超时轮询 (notify_wakes=45 vs timeouts=8963)
3. ⚠️ PPG ~12Hz / IMU ~50Hz 低于配置速率


## PPG / ICM Rate Isolation COMPLETED (2026-05-31)

All 7 experiments run with real hardware SD CSV evidence.

**Results:**
- Case 1 (PPG-only avg4): PPG 12.55 Hz (baseline)
- Case 2 (PPG-only avg1): PPG 50.13 Hz ← **FIFO averaging IS root cause**
- Case 3 (ICM-only): IMU 52.08 Hz (div=21 → 1125/22=51.14)
- Case 4 (ICM-only batch): IMU 52.10 Hz (batch no effect)
- Case 5 (PPG+ICM): PPG 12.56, IMU 52.09 (no mutual slowdown)
- Case 6 (PPG+ICM no-SD): PPG 12.56, IMU 52.09 (SD not bottleneck)
- Case 7 (ECG+PPG+ICM no-MIC): PPG 13.44, IMU 52.22, ECG 488.91

**Root causes:**
1. PPG ~12.5 Hz: MAX30102 FIFO 4-sample averaging (avg4)
2. IMU ~52 Hz: ICM20948 register div=21, ODR=1125/22=51.14 Hz
3. I2C3 sharing: NOT a bottleneck
4. SD writing: NOT a bottleneck
5. FIFO strategy: NOT a factor

**Code changes:**
- Implemented `MultiSensorLogger_WriteRateIsoNoSdCsv()` in `multi_sensor_logger.c`
- 4096-entry PPG/IMU tick ring buffers for no-SD post-recording CSV dump
## In Progress

- Keeping this worklog as the cross-session recovery source.

## PPG / ICM Rate Isolation (2026-05-31)

Goal: run seven isolation experiments for PPG and ICM20948 sampling rate. Do not assume PPG ~12.5 Hz or ICM ~52 Hz are normal until CSV counts, register readbacks, I2C3 mutex timing, task call frequency, and no-SD controls are collected.

Progress:
- Added `python/ppg_icm_rate_report.py` plus unit test to parse SD CSV evidence into `file_count`, `first_tick`, `last_tick`, and `actual_sps`.
- Added CMake override support for `RECORD_RATE_ISO_CASE`, so builds can select experiment cases 1..7 without manual header edits.
- Added rate-isolation macros for the seven requested cases and MAX30102 avg4/avg1 FIFO config selection.
- Added PPG/IMU task call counters, I2C3 mutex wait/hold counters, I2C error counter, and MAX30102/ICM register readback plumbing in firmware.
- Case 1 build (`RECORD_RATE_ISO_CASE=1`, PPG-only avg4) passes.
- Case 1 hardware evidence pulled:
  - `sd_rate_case1_ppg_only_avg4`: PPG CSV `1127` rows, first tick `11864`, last tick `101758`, actual `12.54 Hz`.
  - `sd_rate_case1_ppg_only_avg4_v2`: PPG CSV `800` rows, first tick `11784`, last tick `75633`, actual `12.53 Hz`.

Open issue before continuing cases 2..7:
- `log_001.txt` was not present and `diag_summary.txt` still lacks the new RATE_ISO lines in pulled evidence, despite the strings being present in `build/L496_0405.elf`. Need make register/I2C diagnostics land in an SD artifact reliably, or capture them by OpenOCD memory read before flashing `sd_debug_tool`.

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

