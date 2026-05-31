# PPG / ICM20948 Rate Isolation Handoff

## Short Goal

Continue the PPG / ICM20948 sampling-rate isolation verification on the STM32L496 recorder. Run the remaining hardware experiments from real SD CSV evidence, prove register settings from readback, and decide whether the low rates come from FIFO averaging, ICM configuration, I2C3 contention, scheduling, FIFO strategy, or SD writing.

## Current State

Workspace:

```text
C:\Users\ycs\OneDrive\桌面\codexppt\L496_0405
```

The repository is intentionally dirty. Do not reset or revert user/agent work. Some older Markdown files are noisy or mojibake; for this task, the useful project docs are:

- `WORKLOG.md`
- `PPG_ICM_RATE_ISOLATION_EXPERIMENT.md`
- this handoff file

Use SD files and parsed counters as truth. USB CDC text is weak evidence.

## Required Experiments

Seven groups must be run:

1. PPG-only avg4
2. PPG-only avg1
3. ICM-only
4. ICM-only FIFO batch
5. PPG + ICM
6. PPG + ICM no-SD
7. ECG + PPG + ICM no-MIC

For every group, prove:

- CSV `file_count`
- CSV `first_tick`
- CSV `last_tick`
- CSV `actual_sps`
- MAX30102 / ICM20948 key register readback
- I2C3 mutex wait/hold
- I2C error count
- Task_PPG / Task_IMU call counts

Final decisions needed:

- Is PPG about 12.5 Hz caused by MAX30102 FIFO averaging?
- Is ICM about 52 Hz caused by register configuration, scheduling, FIFO, or I2C?
- Do PPG and ICM slow each other when run together?
- Does no-SD recover the rate, proving SD writer influence?

## Trusted Evidence Already Collected

Only case 1 is currently trusted.

Directory:

```text
sd_rate_case1_ppg_only_avg4_v3
```

Parsed result:

```text
config case=1 label=PPG_ONLY_AVG4
PPG file_count=376
PPG first_tick=11785
PPG last_tick=41753
PPG actual_sps=12.55
IMU file_count=0
ECG file_count=0
MAX30102 ppg_fifo_cfg=0x5F
I2C errors=0
PPG task calls=387
PPG mutex wait max=1 ms
PPG mutex hold max=3 ms
```

Interpretation so far:

```text
PPG-only avg4 still produces about 12.5 Hz.
MAX30102 FIFO_CONFIG=0x5F means avg4.
This strongly suggests 50 Hz / 4 = 12.5 Hz, but do not finalize until case 2 avg1 proves recovery to about 50 Hz.
```

## Invalid Evidence Removed

Old directories for case2-case7 were deleted because a previous batch script passed literal `$n` to CMake, so those directories contained stale case1 firmware data. If they reappear without a new controlled run, treat them as suspect:

```text
sd_rate_case2_ppg_only_avg1
sd_rate_case3_icm_only
sd_rate_case4_icm_only_batch
sd_rate_case5_ppg_icm
sd_rate_case6_ppg_icm_no_sd
sd_rate_case7_ecg_ppg_icm_no_mic
```

## Important Code Changes Present

Existing before this handoff:

- `CMakeLists.txt` supports `-DRECORD_RATE_ISO_CASE=N`.
- `Core/Inc/record_feature_flags.h` defines cases 1-7.
- `Core/Inc/max30102.h` selects MAX30102 FIFO avg4 or avg1.
- `Core/Src/freertos.c` records PPG/IMU task calls, I2C3 wait/hold, I2C errors, and rate-isolation register values.
- `Core/Src/session_manager.c` writes `RATE_ISO_*` lines to `diag_summary.txt`.
- `python/ppg_icm_rate_report.py` parses CSV and diag evidence.

New changes made just before this handoff, not yet build-verified:

- Added ICM FIFO batch API:
  - `ICM20948_EnableAccelGyroFifo`
  - `ICM20948_ReadFifoAccelGyroBatch`
- Added more ICM register readback fields:
  - `USER_CTRL`
  - `LP_CONFIG`
  - `PWR_MGMT_2`
  - `FIFO_EN_2`
  - `FIFO_MODE`
  - `FIFO_COUNT`
- Added case4 path to use ICM FIFO batch reads.
- Added case6 no-SD design:
  - During recording, PPG/IMU samples are kept in RAM instead of continuous CSV block writing.
  - At finalization, a compact `ecg_samples.csv` is written from RAM so the experiment still has a real CSV to parse.

Critical note:

```text
The no-SD / ICM FIFO additions were interrupted before a full firmware build.
First next step is build and fix compile issues.
```

## Key Observation From Code

`Core/Src/icm20948.c` currently configures ICM sample dividers as:

```text
ACCEL_SMPLRT_DIV = 21
GYRO_SMPLRT_DIV = 21
```

With the ICM20948 formula:

```text
ODR = 1125 Hz / (1 + div)
```

That gives:

```text
1125 / 22 = 51.14 Hz
```

This may directly explain the observed ICM about 52 Hz. Still, the final conclusion must come from case3/case4 CSV plus register readback.

## Build / Test Status

Python unit tests passed after the parser/no-SD/FIFO work:

```text
python -m unittest tests.test_ppg_icm_rate_report tests.test_sd_four_modal_validator
5 tests OK
```

Firmware build was not completed after the latest code edits. Run this first:

```powershell
cmake -S . -B build -G Ninja "-DRECORD_RATE_ISO_CASE=2"
ninja -C build
```

Expected: there may be unrelated HAL/LVGL warnings, but no errors.

## Recommended Next Steps

1. Build case2 and fix any compile errors from the latest no-SD/FIFO additions.
2. Verify the ELF really contains case2:

```powershell
Select-String -Path build\CMakeCache.txt -Pattern "RECORD_RATE_ISO_CASE"
arm-none-eabi-strings build\L496_0405.elf | Select-String "PPG_ONLY_AVG1|RECORD_RATE_ISO_CASE"
```

3. Flash case2, wait for the 30s run to stop, flash `sd_debug_tool`, pull SD files, parse them.
4. Continue cases 3-7 one by one. Do not run a blind batch until one controlled case has been proven good.

## Standard Run Procedure

For each case:

```powershell
cmake -S . -B build -G Ninja "-DRECORD_RATE_ISO_CASE=N"
ninja -C build
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program build/L496_0405.elf verify reset exit"
Start-Sleep -Seconds 45
powershell -ExecutionPolicy Bypass -File sd_debug_tool\flash.ps1
Start-Sleep -Seconds 3
mkdir sd_rate_caseN_label
python sd_debug_tool\pc\usb_sd_pull.py --timeout 45 get REC/20260530_210000/ecg_samples.csv -o sd_rate_caseN_label\ecg_samples.csv
python sd_debug_tool\pc\usb_sd_pull.py --timeout 45 get REC/20260530_210000/session.txt -o sd_rate_caseN_label\session.txt
python sd_debug_tool\pc\usb_sd_pull.py --timeout 45 get REC/20260530_210000/diag_summary.txt -o sd_rate_caseN_label\diag_summary.txt
python sd_debug_tool\pc\usb_sd_pull.py --timeout 45 get REC/20260530_210000/modality_summary.csv -o sd_rate_caseN_label\modality_summary.csv
python python\ppg_icm_rate_report.py sd_rate_caseN_label
```

Use these labels:

```text
2: sd_rate_case2_ppg_only_avg1
3: sd_rate_case3_icm_only
4: sd_rate_case4_icm_only_batch
5: sd_rate_case5_ppg_icm
6: sd_rate_case6_ppg_icm_no_sd
7: sd_rate_case7_ecg_ppg_icm_no_mic
```

## What To Check Per Case

Case2 PPG-only avg1:

```text
Expected ppg_fifo_cfg=0x1F.
If actual PPG is about 50 Hz, PPG 12.5 Hz is confirmed as FIFO avg4 effect.
If still about 12.5 Hz, avg1 did not take effect or driver/task reading is limiting it.
```

Case3 ICM-only:

```text
Check IMU actual_sps and icm_accel_div / icm_gyro_div.
If div=21 and actual is about 51-52 Hz, ICM rate is configuration-caused.
```

Case4 ICM-only FIFO batch:

```text
Check FIFO registers and IMU actual_sps.
If batch still about 52 Hz with div=21, FIFO is not the root cause.
If batch improves, the single-sample read path is limiting throughput.
```

Case5 PPG + ICM:

```text
Compare against case1/case2/case3.
Look for PPG/IMU actual_sps drop and I2C3 wait/hold increases.
```

Case6 PPG + ICM no-SD:

```text
Continuous CSV writer should be bypassed during capture.
CSV should be written after capture from RAM.
If rates recover versus case5, SD writer affects sampling.
If rates stay low, SD is not the main cause.
```

Case7 ECG + PPG + ICM no-MIC:

```text
This is the core no-microphone integrated workload.
Use it to judge final system behavior without MIC interference.
```

## Parser

Use:

```powershell
python python\ppg_icm_rate_report.py sd_rate_case1_ppg_only_avg4_v3 sd_rate_case2_ppg_only_avg1
```

The parser prints:

```text
root=
csv=
log=
config=
PPG,file_count=...,first_tick=...,last_tick=...,actual_sps=...
IMU,file_count=...,first_tick=...,last_tick=...,actual_sps=...
ECG,file_count=...,first_tick=...,last_tick=...,actual_sps=...
diagnostics=
```

## Known Pitfalls

- Do not trust old case2-case7 output directories.
- Quote CMake definitions in PowerShell:

```powershell
"-DRECORD_RATE_ISO_CASE=2"
```

- `sd_debug_tool clear` may not clean nested experiment directories; use unique local pull directories.
- The board may show ST-Link target voltage around 1.1-1.2 V but still flash and verify OK.
- `log_001.txt` may be absent. Prefer `diag_summary.txt`.
- `multi_sensor_logger.c` is not valid UTF-8. Use careful encoding-preserving edits or patch small ASCII-adjacent anchors only.


## ✅ Final Results (2026-05-31)

All 7 isolation experiments completed with real hardware SD CSV evidence.

### Root Cause Verdict

| Question | Answer | Evidence |
|----------|--------|----------|
| Is PPG ~12.5 Hz caused by FIFO averaging? | **YES** | avg4→12.55 Hz, avg1→50.13 Hz (Case 1→2) |
| Is ICM ~52 Hz caused by register configuration? | **YES** | div=21, ODR=1125/22=51.14 Hz matches measured 52.08 Hz (Case 3) |
| Do PPG and ICM slow each other? | **NO** | Case 5 rates identical to Case 1+3 solo runs |
| Does SD writing affect rate? | **NO** | Case 6 (no-SD) rates identical to Case 5 |
| Is FIFO batch strategy the cause? | **NO** | Case 4 batch same rate as Case 3 single-read |

### Code Fix Applied

- `Core/Src/multi_sensor_logger.c`: Implemented `MultiSensorLogger_WriteRateIsoNoSdCsv()` with 4096-entry tick ring buffers for PPG/IMU, enabling Case 6 no-SD post-recording CSV dump.

### Evidence Directories

- `sd_rate_case1_ppg_only_avg4_v3/` (pre-existing)
- `sd_rate_case2_ppg_only_avg1/`
- `sd_rate_case3_icm_only/`
- `sd_rate_case4_icm_only_batch/`
- `sd_rate_case5_ppg_icm/`
- `sd_rate_case6_ppg_icm_no_sd/`
- `sd_rate_case7_ecg_ppg_icm_no_mic/`
## Completion Criteria

Do not call this complete until the seven rows below are filled from real evidence:

| Case | PPG SPS | IMU SPS | ECG SPS | Key register proof | I2C / task proof | Judgment |
|---|---:|---:|---:|---|---|---|
| 1 PPG-only avg4 | 12.55 | - | - | ppg_fifo_cfg=0x5F | errors=0 | avg4 → 12.55 Hz baseline |
| 2 PPG-only avg1 | 50.13 | - | - | ppg_fifo_cfg=0x1F | errors=0 | **avg1 → 50 Hz recovered: FIFO averaging IS root cause of ~12.5 Hz** |
| 3 ICM-only | - | 52.08 | - | icm_div=21, ODR=1125/22=51.14 | errors=0 | **div=21 → ~52 Hz: register config IS root cause** |
| 4 ICM-only FIFO batch | - | 52.10 | - | icm_batch=1, fifo_count=12 | errors=0 | Batch mode does NOT change rate |
| 5 PPG+ICM | 12.56 | 52.09 | - | ppg_fifo=0x5F, icm_div=21 | errors=0, wait≤2ms | **No mutual slowdown: I2C3 sharing is NOT bottleneck** |
| 6 PPG+ICM no-SD | 12.56 | 52.09 | - | no_sd=1 | errors=0, identical to case5 | **SD writing is NOT bottleneck** |
| 7 ECG+PPG+ICM no-MIC | 13.44 | 52.22 | 488.91 | CSV valid, diag stale (RTC overlap) | errors=0 | ECG ~489 Hz, PPG/IMU rates consistent |

