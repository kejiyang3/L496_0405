# L496_0405 Debug Guide

Last consolidated: 2026-05-28

## Core Rule

Treat the firmware as a data recorder first. A loop is successful only when SD artifacts, session manifests, and counters support the claim. USB CDC text is weak evidence because it can swallow, truncate, reorder, or merge logs.

## Standard Closed-Loop Flow

The full detailed process is documented in WORKLOG.md under 'Closed-Loop Development Process'. Quick version below.


1. Read `README.md`, `WORKLOG.md`, and this file.
2. Inspect relevant source before changing code.
3. Make a small scoped change.
4. Build:

```powershell
ninja -C build
```

5. Flash only when hardware validation is needed:

```powershell
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "program build/L496_0405.elf verify reset exit"
```

Recent known-good CubeIDE OpenOCD command on this machine:

```powershell
C:\ST\STM32CubeIDE_1.16.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.externaltools.openocd.win32_2.4.100.202501161620\tools\bin\openocd.exe -s C:\ST\STM32CubeIDE_1.16.1\STM32CubeIDE\plugins\com.st.stm32cube.ide.mcu.debug.openocd_2.3.100.202501240831\resources\openocd\st_scripts -f interface/stlink-dap.cfg -f target/stm32l4x.cfg -c "program build/L496_0405.elf verify reset exit"
```

Do not use `STM32_Programmer_CLI` for normal closed-loop runs because it can disturb USB CDC/COM monitoring.

6. Wait for the 10-second boot auto-record flow, or run the user-requested closed-loop duration.
7. Prefer evidence from:
   - `session_XXX.txt`
   - `log_XXX.txt`
   - `ecg_XXX.csv`
   - `mic_XXX.wav`
   - `MULTI_STATS`
   - `PPG_STATS`
   - `IMU_STATS`
   - `MIC_STATS`
   - OpenOCD/GDB memory counters
8. Update `WORKLOG.md` after a meaningful stage.

When using USB CDC monitoring, start the monitor before flashing or reset so early boot logs are not missed. Default CDC has been `COM12`, `115200`, but always check ports if uncertain.


## SD Card Access Without Removal (sd_debug_tool)

When the SD card needs to be read without physically removing it from the device, use the `sd_debug_tool` located at `L496_0405\sd_debug_tool\`.

**Workflow:**

1. Flash the debug firmware:
```powershell
cd L496_0405\sd_debug_tool
.\flash.ps1
```

2. Pull files over USB CDC:
```powershell
python pc\usb_sd_pull.py list                     # List all files
python pc\usb_sd_pull.py get-all -o H:\sd_pull    # Download everything
python pc\usb_sd_pull.py cat log_001.txt          # View text files
```

3. Flash back the main firmware when done.

**Features:**
- ACK + CRC-16/XMODEM retransmission for reliable large-file transfer
- Automatic resume from interruption (up to 3 retries)
- Real-time progress bar
- Batch download (`get-all`)

**Evidence priority:** SD artifacts pulled through this tool are trusted evidence, same as PC-mounted SD reads. USB CDC unreliability does not apply to this tool because of the ACK protocol.



List serial ports:

```powershell
python python/stm32_cdc_monitor.py --list-ports
```

Monitor CDC:

```powershell
$env:PYTHONIOENCODING='utf-8'
python python/stm32_cdc_monitor.py -p COM12 -l logs/cdc_xxx.log
```

Reset through OpenOCD:

```powershell
openocd -f interface/stlink.cfg -f target/stm32l4x.cfg -c "init" -c "reset" -c "resume" -c "shutdown"
```

Validate PC-mounted SD after fail-fast/four-modal work:

```powershell
python python\sd_four_modal_validator.py H:\
```

## Pass Conditions

Short four-modal run:

- `session_XXX.txt` exists and matches `ecg_XXX.csv`, `mic_XXX.wav`, and `log_XXX.txt`.
- ECG rows are nonzero and around the expected 10-second count.
- PPG rows are nonzero and overlap the recording window.
- IMU rows are nonzero and overlap the recording window.
- MIC WAV duration is close to session duration.
- Write failures and block drops are zero or explicitly explained.

10-minute default run:

- `g_ecg_rec.state` becomes `ECG_REC_STOPPED` after about `600000 ms`.
- `sd_file_opened=0` and `sd_file_closed=1`.
- `session_XXX.txt`, `ecg_XXX.csv`, and `mic_XXX.wav` validate after mounting SD on PC.

Long run:

- All required modalities remain alive for the intended duration.
- The system does not silently continue as ECG-only after MIC, PPG, or IMU failure.
- If a modality fails, the session stops promptly and preserves existing files.
- MIC file is not reopened/truncated under the same sequence.

## Hardware Diagnostics

### TXS0104 I2C/Interrupt Mapping

| Signal | Sensor side | STM32 side |
|---|---|---|
| I2C_SCL | sensor SCL | PC0 |
| I2C_SDA | sensor SDA | PC1 |
| PPG_INT | MAX30102 INT | PC2 |
| ICM_INT | ICM20948 INT | PH1 |

TXS0104 requirements:

- VCCA = 1.8 V.
- VCCB = 3.3 V.
- OE must be pulled high to VCCA.
- Grounds must be common.

INT line diagnosis with meter or scope:

| A side | B side | Interpretation |
|---|---|---|
| LOW | LOW | Sensor is pulling interrupt low or pending status is uncleared |
| HIGH | LOW | TXS, OE, or STM32 pin-side issue |
| LOW | HIGH | TXS channel abnormal |
| HIGH | HIGH | Hardware line is idle/normal |

### MAX30102 INT Protocol

- MAX30102 INT is open-drain, active low.
- Power-ready can assert INT low after power-up.
- Reading `INTERRUPT_STATUS1` releases the INT line.
- If INT stays low, EXTI falling edge will not retrigger.
- Current firmware enables `A_FULL + PPG_RDY` during recording so PC2 is the primary PPG read trigger.
- Timeout drain is only a fallback for a stuck-low or missed-edge state.

### ICM20948 INT Protocol

- ICM20948 Data Ready uses `PH1 / EXTI1`.
- ISR only increments `icm_irq_count` and notifies `Task_IMU`.
- I2C reads happen in task context after notification.
- Current IMU recording is interrupt-driven; polling should not be reintroduced without a deliberate test.

## SD Write Safety

- All multi-writer SD paths share `Mtx_SDCardHandle`.
- Do not call `f_mount(NULL, ...)` in stop/error paths while another file may be open.
- CSV writer uses chunked writes to reduce SD mutex hold time.
- `.sram2` is used for microphone DMA and must remain `NOLOAD`; otherwise OpenOCD can try to verify SRAM2 and fail flashing.

## Failure Patterns And Root Causes

### USB CDC unreliability

Symptoms:

- Missing lines.
- Truncated lines.
- Multiple lines merged.
- Silence even while firmware is running.

Rule: absence of a USB line is not proof that code did not run.

### I2C3 shared-bus instability

Symptoms:

- `MAX30102_INIT_NOT_FOUND`.
- `ICM20948_INIT_FAILED`.
- Large PPG FIFO/I2C failure counts.
- PPG/IMU stop while ECG continues.

Current mitigations:

- `Mtx_I2C3` protects runtime access.
- I2C3 bus recovery sends SCL pulses and STOP before init retries.
- Four-modal start is blocked if required PPG/IMU are not ready.
- Repeated runtime failures stop the session instead of allowing silent degradation.

### Audio truncation after runtime failure

Root cause found on overnight run:

- Audio task could return to top while recording was still active.
- It reopened `mic_003.wav` with `FA_CREATE_ALWAYS`.
- The earlier long audio data was truncated.

Current mitigation:

- Runtime audio write/sync failure requests whole-session stop.
- Audio task refuses to reopen the same WAV sequence after runtime failure.

### Screen STOP touch unreliability

Observed issue:

- After about 10 minutes, screen still ran but the STOP button did not respond.
- Likely path: `touch_int_flag` was set by EXTI, but `touchpad_read()` re-checked CST816 INT after the pulse returned high and discarded the touch.

Mitigations:

- Latched touch IRQ forces one CST816 controller read.
- STOP is issued on `LV_EVENT_PRESSED`, not only `LV_EVENT_CLICKED`.
- `RECORD_DEFAULT_RECORD_MS=600000U` provides a hardware-verified auto-stop fallback so SD closure does not depend on touch STOP.

Hardware-verified auto-stop evidence:

- Start/stop tick delta about `600198 ms`.
- Final state `ECG_REC_STOPPED`.
- SD file flags closed.
- PPG/IMU/ECG counters remained alive through the run.

## Useful Register Notes

MAX30003:

| Register | Address | Meaning |
|---|---:|---|
| STATUS | `0x01` | EINT, EOVF, DCLOFFINT, PLLINT |
| EN_INT | `0x02` | interrupt enable bits |
| MNGR_INT | `0x04` | FIFO interrupt threshold |
| CNFG_ECG | `0x15` | ECG rate/gain config |
| ECG_FIFO_BURST | `0x20` | FIFO burst read |

MAX30102:

| Register | Address | Meaning |
|---|---:|---|
| INTERRUPT_STATUS1 | `0x00` | read to clear/release INT |
| INTERRUPT_ENABLE1 | `0x02` | current recording value `0xC0` |
| FIFO_WR_POINTER | `0x04` | FIFO write pointer |
| FIFO_OV_COUNTER | `0x05` | FIFO overflow counter |
| FIFO_RD_POINTER | `0x06` | FIFO read pointer |
| FIFO_DATA | `0x07` | RED/IR FIFO data |
| FIFO_CONFIGURATION | `0x08` | current stable value `0x5F` |
| SPO2_CONFIGURATION | `0x0A` | current stable value `0x22` |

## Toolchain Setup

Install if needed:

```powershell
winget install -e --id ARM.GnuToolchain
winget install -e --id Kitware.CMake
winget install -e --id Ninja-build.Ninja
winget install -e --id xpack-dev-tools.openocd-xpack
winget install -e --id Python.Python
pip install pyserial
```

Verify:

```powershell
arm-none-eabi-gcc --version
cmake --version
ninja --version
openocd --version
python -c "import serial; print('ok')"
```
