# L496_0405 Feature Notes

Last consolidated: 2026-05-28

This file keeps archived feature context that is useful but too detailed for `README.md`.

## Current Development Route

1. Preserve stable four-modal short recording.
2. Validate each SD card after PC mount using `python\sd_four_modal_validator.py H:\`.
3. Use the 10-minute auto-stop default for touch-independent validation.
4. Run another long recording only after 10-minute SD-file validation passes.
5. Treat I2C3 speed-up and PPG sample-rate increase as separate closed-loop experiments.
6. Keep UI/LVGL work secondary to recorder correctness.

## Four-Modal Recording Stabilization

Goal: ECG + MIC + ICM20948 + MAX30102 should read and persist together.

Current approach:

- ECG stays on MAX30003 SPI3 path.
- MIC stays in separate WAV path using SRAM2 SAI DMA.
- IMU uses `ICM_INT=PH1/EXTI1` Data Ready interrupts.
- PPG uses `PPG_INT=PC2/EXTI2` as the primary MAX30102 FIFO drain trigger.
- MAX30102 and ICM20948 share I2C3 through TXS0104, so I2C3 access is mutex-protected.
- I2C3 speed remains conservative until stability is proven.

Important verified short-run evidence from 2026-05-27:

- `g_icm_init_ret=0`
- `g_ppg_init_ret=0`
- `s_ppg_ready=1`
- `s_icm_ready=1`
- `ppg_irq_count=125`
- `g_ppg_int_wakeup_count=125`
- `g_ppg_timeout_wakeup_count=0`
- `g_ppg_timeout_drain_count=0`
- ECG write ok `5104`
- PPG samples/write ok `125`
- IMU read/write ok `521`
- PPG FIFO read fail `0`
- PPG FIFO empty `0`

PPG note: output rate is intentionally low first. With no FIFO averaging, I2C3/TXS0104 pressure increased and IMU sample count dropped. The stable baseline uses MAX30102 FIFO config `0x5F` with 4-sample averaging.

## Overnight SD Debug 2026-05-28

Evidence source:

- SD card mounted as `H:\`.
- Inspected `session_001.txt` through `session_005.txt`, matching logs, CSVs, and WAVs.
- SD artifacts were trusted over USB CDC.

Findings:

- `seq=001`: short four-modal run survived.
  - Duration `10004 ms`.
  - CSV rows: ECG `5105`, PPG `126`, IMU `521`.
  - MIC `376832 bytes`, `46 halves`, WAV about `10.264 s`.
- `seq=002`: aborted/near-empty session, duration `3 ms`.
- `seq=003`: long session around `11.16 h`, but only ECG stayed alive for the full duration.
  - ECG rows `10843472`.
  - PPG rows only `92`, stopping around `7.2 s`.
  - IMU rows only `382`, stopping around `7.3 s`.
  - MIC was originally much larger, then `mic_003.wav` was reopened/truncated to about `90.45 s`.
  - PPG read failures reached `249402`.
- `seq=004` and `seq=005`: ECG + MIC only, PPG/IMU zero.

Root causes:

1. Audio runtime write/sync failure did not stop the whole session.
2. Audio task could reopen/truncate the same WAV sequence after failure.
3. PPG/IMU runtime failures did not fail-fast the session.
4. The system could report a long session even when only ECG remained alive.

Firmware changes made after the overnight analysis:

- Added fail-fast policy macros in `Core/Inc/record_feature_flags.h`.
- Added `tests/record_fail_fast_compile_test.c`.
- Audio runtime write/sync errors now request whole-session stop.
- Audio task refuses to reopen the same WAV sequence after runtime failure.
- Start gate retries PPG/IMU when not ready.
- Required four-modal start is blocked if PPG/IMU readiness still fails.
- PPG/IMU repeated runtime failures trigger I2C3 recovery, then session stop.
- Runtime failure streaks are cleared between sessions, and failed modalities must be reinitialized before the next session.

Verification after implementation:

- `record_fail_fast_compile_test.c` passed.
- `audio_format_compile_test.c` passed.
- `imu_record_mode_compile_test.c` passed.
- `ninja -C build` passed.
- OpenOCD flashed `build/L496_0405.elf` and reported `Verified OK`.
- Follow-up memory counters after reset/auto-record:
  - `g_ecg_rec.state = ECG_REC_STOPPED`
  - `file_seq = 2`
  - ECG sample/write `2774`
  - MIC bytes `368640`, write errors `0`
  - PPG irq/read ok `132`, fail `0`
  - IMU read ok `545`, fail `0`

Remaining acceptance check:

```powershell
python python\sd_four_modal_validator.py H:\
```

Short SD-file acceptance later passed for `seq=001`:

- `session_001.txt`: duration `10434 ms`, ECG `2774`, PPG `132`, IMU `545`, MIC `368640 bytes`.
- All write failures and drops were `0`.
- `ecg_001.csv`: ECG span `9425 ms`, PPG span `10415 ms`, IMU span `10420 ms`.
- `mic_001.wav`: mono 16-bit PCM, `184320` frames at effective `18682 Hz`, duration about `9.87 s`.

## 10-Minute Auto-Stop Fallback

Reason:

- The screen STOP button remained unreliable during recording.
- The recorder needs a validation path that does not depend on touch input.

Changes:

- Added `RECORD_DEFAULT_RECORD_MS 600000U` in `Core/Inc/record_feature_flags.h`.
- Added `tests/record_duration_policy_compile_test.c`.
- Updated `ECG_RequestStart()` and boot auto-record setup to use the default duration policy.

Verification:

- `record_duration_policy_compile_test.c` passed.
- `record_fail_fast_compile_test.c` passed.
- `lvgl_acceptance_ui_compile_test.c` passed.
- `ninja -C build` passed.
- OpenOCD flash reported `Verified OK`.
- Pre-duration memory check showed recording active and `auto_stop_ms=600000`.
- After about 620 seconds, memory check showed:
  - `g_ecg_rec.state=ECG_REC_STOPPED`
  - start/stop tick delta about `600198 ms`
  - `sd_file_opened=0`
  - `sd_file_closed=1`
  - MIC bytes `0x0155e000`
  - ECG sample/write `0x265d4`
  - PPG read ok `0x1dab`
  - IMU read ok `0x7a3e`

Conclusion: 10-minute automatic stop is hardware-verified and closes SD recording without screen STOP.

Next check: mount the SD card and validate `session_XXX.txt`, combined `ecg_XXX.csv`, and `mic_XXX.wav` from the 10-minute run.

## Audio Long-Run Debug

Original long-run evidence from `H:\`:

- `ecg_002.csv`: `921883501 bytes`, about `13.308 h`, ECG rate about `510.35 Hz`.
- `mic_002.wav`: `433389612 bytes`, WAV header `16000 Hz`, player duration about `3.762 h`.
- Effective audio rate over ECG duration was about `4523 Hz`.
- Sample values were only `0..255`, proving the SAI 24-bit to PCM16 conversion was wrong.

Root causes:

1. Signed 24-bit SAI samples were converted incorrectly.
2. DMA half-complete events were represented as notification bits, so repeated half events could collapse while SD writes blocked.

Changes made:

- Added `Core/Inc/audio_sample_format.h`.
- Added `tests/audio_format_compile_test.c`.
- Updated `Core/Src/audio_recorder.c` to use correct signed 24-bit to PCM16 conversion.
- Added missed DMA half accounting into `mic_drops`.
- Lowered bad-capture WAV rate clamp to `1000 Hz`.
- Reduced periodic WAV header sync pressure.
- Chunked multi-sensor CSV writes to reduce SD mutex hold time.

Short hardware validation after fix:

- First run: ECG `5110`, IMU `501`, MIC `393216 bytes`, `48 halves`, drops `0`, `maxwr=35 ms`, PCM range `-705..1773`.
- Reset run: ECG `5111`, MIC `393216 bytes`, `48 halves`, drops `0`, `maxwr=94 ms`, PCM range `-1316..1806`.

Remaining risk: the original failure was hours long, so long-run validation is still required after short SD-file acceptance is clean.

## IMU Interrupt-Driven Recording

Goal: replace 20 ms polling with ICM20948 Data Ready interrupts.

Current behavior:

- `ICM_INT=PH1/EXTI1` notifies `Task_IMU`.
- `StartTask_IMU()` reads accel/gyro only after notification.
- Recording start clears stale notifications/EXTI/status and enables Data Ready after ECG stream starts.
- Recording stop disables Data Ready before flushing.
- IMU block capacity uses `IMU_SAMPLE_RATE_HZ=104`, `IMU_BLOCK_SAMPLES=208`.

Verified short-run evidence:

- First run: `icm_irq_count=522`, `g_imu_read_ok_count=521`, read fail `0`, write ok `521`, block drop `0`.
- Reset run: `icm_irq_count=521`, `g_imu_read_ok_count=521`, read fail `0`, write ok `521`, block drop `0`.

## LVGL UI Plan

Current UI is a recorder dashboard. The larger planned UI redesign is not the active priority, but the design is preserved here.

Recent STOP-button hardening:

- `App/LVGL/lv_port_indev.c`: latched CST816 touch IRQ forces one controller read.
- `App/LVGL/app_lvgl.c`: STOP is issued on press instead of waiting for click/release.
- Acceptance macros were added in `App/LVGL/app_lvgl_acceptance_ui.h`:
  - `APP_LVGL_STOP_ON_PRESS`
  - `APP_LVGL_TOUCH_IRQ_FORCES_READ`
- Locked with `tests/lvgl_acceptance_ui_compile_test.c`.

Even with this hardening, the 10-minute auto-stop policy is the current reliable validation path.

Target display:

- ST7789V, `240x280`, RGB565.
- LVGL v8.3.11.
- CST816 touch gestures: `0x03` right swipe, `0x04` left swipe.

Planned three-page UI:

1. Watch face:
   - status bar
   - large time
   - date
   - record indicator
   - start button
2. ECG live:
   - BPM
   - scrolling ECG chart
   - recording elapsed time
   - stop button
3. Device info:
   - lead state
   - file sequence
   - sampled/written/dropped/EOVF stats
   - SD and elapsed status

Planned code units:

- `App/LVGL/ecg_chart_buffer.h/.c`: single-producer/single-consumer ECG sample ring buffer.
- `App/LVGL/watch_time.h/.c`: software clock until RTC/BLE sync exists.
- `App/LVGL/app_lvgl.c`: three full-screen containers, page dots, chart, cards.
- `lv_conf.h`: enable `LV_USE_CHART` and required fonts.
- `CMakeLists.txt`: add new LVGL helper source files.

Memory budget from the original plan:

- ECG chart ring: about `4 KB`.
- LV_CHART + points: about `1.2 KB` LVGL heap.
- Widgets: about `3 KB` LVGL heap.
- Fonts: about `30 KB` flash.

UI cautions:

- Keep LVGL update paths allocation-light.
- Avoid object creation/deletion in frequent timers.
- Avoid heavy I2C calls from UI timers.
- Keep a live counter visible so UI stalls are obvious.

## Future Architecture Idea

A long-term cleaner design is a single binary session container:

- `session_XXX.bin` with typed packets for ECG, PPG, IMU, and MIC.
- One SD writer owns all SD writes.
- Optional post-processing/export to CSV and WAV.

This could reduce SD contention and avoid slow text CSV overhead, but it is a larger redesign and should wait until the current four-modal recorder behavior is stable.
