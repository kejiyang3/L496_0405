# L496_0405 Firmware README

Last consolidated: 2026-05-28

## Summary

`L496_0405` is STM32L496 wearable recorder firmware using FreeRTOS, FatFS/SD, LVGL, MAX30003 ECG, SAI microphone audio, ICM20948 IMU, and MAX30102 PPG.

The current target baseline is four-modal recording:

- ECG: MAX30003 on SPI3, persisted as `ECG` rows in `0:/ecg_XXX.csv`.
- MIC: SAI1 microphone, persisted as `0:/mic_XXX.wav`.
- IMU: ICM20948 on I2C3, persisted as `IMU` rows in `0:/ecg_XXX.csv`.
- PPG: MAX30102 on I2C3, persisted as `PPG` rows in `0:/ecg_XXX.csv`.

Each recording also writes:

- `0:/log_XXX.txt`: per-session debug evidence.
- `0:/session_XXX.txt`: final manifest for the same file sequence.

Evidence priority is SD/session/log/counters first. USB CDC is weak evidence only.

Current duration policy:

- Normal recording starts use `RECORD_DEFAULT_RECORD_MS = 600000U`.
- Recordings auto-stop after 10 minutes, so validation does not depend on the screen STOP button.
- Older 10-second boot auto-record notes are historical unless the feature flag is changed again.

## Build And Flash

From `C:\Users\ycs\OneDrive\桌面\codexppt\L496_0405`:

```powershell
ninja -C build
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program build/L496_0405.elf verify reset exit"
```

If the build directory needs first-time configuration:

```powershell
cd L496_0405
mkdir build
cd build
cmake .. -G Ninja
cd ..
```

Known OpenOCD caveat: ST-Link often reports target voltage around `1.15 V`; flashing has still verified OK in recent loops.

Recent reliable OpenOCD path on this machine uses CubeIDE's bundled OpenOCD and `interface/stlink-dap.cfg`; see `DEBUG_GUIDE.md` for the full command.


## SD Card Access (Without Removal)

Use the `L496_0405\sd_debug_tool\` to read the SD card over USB CDC without physically removing it:

```powershell
cd L496_0405\sd_debug_tool
.\flash.ps1                                          # Flash debug firmware
python pc\usb_sd_pull.py get-all -o ..\sd_pull_output  # Pull all files
# ... then flash back main firmware
```

The tool uses ACK + CRC-16 retransmission, so USB CDC unreliability is no longer a concern. See `DEBUG_GUIDE.md` for the full workflow.



| Module | Device | Bus | Interrupt / Pin | Current role |
|---|---|---|---|---|
| ECG | MAX30003 | SPI3 | `PB6 / EXTI9_5` | 512 SPS ECG recording |
| MIC | SAI mic | SAI1 + DMA | DMA half/full | WAV recording in SRAM2 double buffer |
| IMU | ICM20948 | I2C3 via TXS0104 | `PH1 / EXTI1` | Data Ready interrupt-driven accel/gyro |
| PPG | MAX30102 | I2C3 via TXS0104 | `PC2 / EXTI2` | PPG FIFO drain, interrupt-triggered |
| Display | ST7789V | SPI1 + DMA | none | LVGL dashboard |
| Touch | CST816 | I2C2 | touch interrupt | optional UI input |
| Storage | SD card | SDMMC + FatFS | none | CSV, WAV, logs, session manifests |
| Debug | USB CDC | USB FS | none | unreliable under sustained output |

I2C3 is shared by MAX30102 and ICM20948 through TXS0104. Runtime access must stay mutex-protected by `Mtx_I2C3`.

## Current Data Flow

1. `StartTask_Sensor()` initializes SD debug logging and sensors, then requests boot auto-recording.
2. `ECG_UpdateFileName()` chooses `ecg_XXX.csv`; `SD_DebugLog_StartNewFile()` chooses `log_XXX.txt`.
3. `MultiSensorLogger_ResetForNewRecording()` clears per-session counters.
4. `StartTask_MultiSensor_SDWriter()` opens CSV, writes header and `META_START`.
5. `AudioRecorder_Task()` opens `mic_XXX.wav`, waits for ECG stream start, then starts SAI DMA.
6. `MAX30003_StartStream()` starts ECG; ECG IRQ notifies the sensor task.
7. ECG rows go through `MAX30003_Task()` -> `MultiSensorLogger_AddECG()`.
8. IMU rows are read only after `ICM_INT` task notifications and written through `MultiSensorLogger_AddIMU()`.
9. PPG rows are read mainly after `PPG_INT` task notifications; timeout drain is only a stuck-low fallback.
10. Stop path disables sensor interrupts, flushes partial blocks, closes CSV and WAV, writes `MULTI_STATS`, `PPG_STATS`, `IMU_STATS`, `MIC_STATS`, and `session_XXX.txt`.

## Current Stability Policy

- Boot auto-recording must be preserved unless explicitly changed.
- Normal start auto-stops after 10 minutes via `RECORD_DEFAULT_RECORD_MS`.
- Four-modal mode should not silently degrade into ECG-only long sessions.
- Runtime failure in MIC, PPG, or IMU should stop the session promptly and preserve files already written.
- Audio must not reopen/truncate an existing `mic_XXX.wav` after runtime failure.
- PPG and IMU must be ready before a required four-modal recording starts.
- PPG/IMU repeated runtime I2C/FIFO failures trigger I2C3 recovery, then fail-fast stop if still failing.

## MAX30102 / ICM20948 Shared I2C3 Notes

Current stable PPG strategy:

- I2C3 timing is still the existing project timing; speed-up is a separate future closed-loop step.
- MAX30102 uses low-bandwidth stable mode first:
  - `SPO2_CONFIG = 0x22`
  - FIFO config `0x5F` with 4-sample averaging
  - `INT_ENABLE1 = 0xC0` (`A_FULL + PPG_RDY`)
- `PPG_INT=PC2/EXTI2` is the primary PPG read trigger.
- Timeout path only drains if `PPG_INT` is stuck low.

Current stable IMU strategy:

- `ICM_INT=PH1/EXTI1` is the primary IMU read trigger.
- `StartTask_IMU()` waits for task notifications and calls `ICM20948_ReadAccelGyroRaw()` only after Data Ready.
- Do not reintroduce 20 ms polling unless explicitly requested.

## Key Source Files

| File | Responsibility |
|---|---|
| `L496_0405/Core/Src/freertos.c` | FreeRTOS tasks, start/stop state machine, sensor coordination |
| `L496_0405/Core/Src/main.c` | HAL init, EXTI callbacks, IRQ counters |
| `L496_0405/Core/Src/max3003.c` | MAX30003 ECG driver |
| `L496_0405/Core/Src/Max30102.c` | MAX30102 PPG driver and FIFO drain |
| `L496_0405/Core/Src/icm20948.c` | ICM20948 I2C driver |
| `L496_0405/Core/Src/multi_sensor_logger.c` | ECG/PPG/IMU buffering, CSV writer, counters |
| `L496_0405/Core/Src/audio_recorder.c` | SAI microphone WAV writer |
| `L496_0405/Core/Src/sd_debug_log.c` | SD log, snapshots, session summary |
| `L496_0405/Core/Inc/record_feature_flags.h` | Recording feature and fail-fast policy flags |
| `L496_0405/App/LVGL/lv_port_indev.c` | CST816 touch read path |
| `L496_0405/App/LVGL/app_lvgl.c` | LVGL UI and STOP behavior |
| `L496_0405/CMakeLists.txt` | Real top-level source list |
| `L496_0405/.em_skill.json` | embed-ai-tool project tool/profile config |

## Document Map

Project-owned Markdown is intentionally kept to four files:

- `README.md`: current project map and baseline.
- `WORKLOG.md`: chronological evidence, closed-loop results, recovery prompt.
- `DEBUG_GUIDE.md`: build/flash/debug workflow and hardware diagnostics.
- `FEATURE_NOTES.md`: archived feature notes, long-run findings, UI plan, future route.

Third-party Markdown under `L496_0405/Drivers` and `L496_0405/Middlewares` is dependency documentation and should stay in place.
