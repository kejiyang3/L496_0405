# Case F SD Write Blocking ? Experiment Report

## Date: 2026-05-29
## Commit: f423642

---

## Experiment Summary

| Experiment | Flags | ECG Rate | task_calls | max_gap | eovf | notify_wakes | notify_t/o | csv_wait | csv_hold |
|-----------|-------|----------|------------|---------|------|-------------|------------|----------|----------|
| **Original Case F** | CASE=6, all off | **74.7 Hz** | 583 | 1008ms | 25 | - | - | - | - |
| **F1** (skip f_write) | CASE=6 + DROP_BEFORE_FWRITE=1 | **~75 Hz** | 556 | 1002ms | 25 | 51 | 505 | 1000ms | 1002ms |
| **F2** (raw f_write) | CASE=6 + WRITE_ONLY_NO_SYNC=1 | **~75 Hz** | 584 | 1002ms | 25 | 58 | 526 | 1000ms | 1002ms |
| **F2 + FS_TIMEOUT=10** | + _FS_TIMEOUT=10 | **51 Hz** | 410 | 1002ms | 26 | 54 | 356 | 997ms | - |
| **F2 + FS_TIMEOUT=50+MUTEX** | + _FS_TIMEOUT=50, _USE_MUTEX=1 | **89 Hz** | 697 | 1002ms | 25 | 59 | 638 | 1000ms | 1002ms |
| **P3** (non-blocking) | CASE=6 + NONBLOCKING + FS50+MUTEX | **130.7 Hz** | 1010 | 1002ms | 22 | 65 | 945 | **9ms** | **45ms** |

### Baseline (from prior runs)

| Case | ECG Rate | Conditions |
|------|----------|------------|
| **A** (no Audio) | **374.8 Hz** | Audio OFF, MIC OFF, SAI DMA OFF |
| **C** (Audio idle) | **374.4 Hz** | AudioTask idle, MIC OFF, SAI DMA OFF |
| **D** (SAI DMA ON) | **393 Hz** | EN_MIC ON, SAI DMA ON, no pack |
| **E** (MIC Pack ON) | **376.3 Hz** | EN_MIC ON, SAI DMA ON, pack ON, no SD write |

---

## Key Findings

### 1. f_write is NOT the primary trigger

F1 skips `f_write` entirely in `audio_write_locked`. If SD write were the bottleneck, F1 should recover to ~375 Hz. Instead, F1 stayed at ~75 Hz (same as original Case F). This rules out `f_write` data transfer as the root cause.

### 2. FatFS _FS_TIMEOUT=1000 is a major contributor

The original `ffconf.h` had `_FS_TIMEOUT=1000` (1 second) for FatFS internal semaphore. When multiple tasks (AudioTask, MSWriter, DiagLog in SensorTask) contend for the FatFS semaphore, any caller can block for up to 1000ms.

- Reducing to `_FS_TIMEOUT=10` made things **worse** (51 Hz) ? FatFS operations timed out too aggressively
- Reducing to `_FS_TIMEOUT=50` + `_USE_MUTEX=1` (priority inheritance) improved to 89 Hz

### 3. CSV writer osWaitForever exacerbates contention

`sd_write_records_checked` used `osMutexAcquire(Mtx_SDCardHandle, osWaitForever)` for the external SD mutex. With `csv_wait_max=1000ms`, the CSV writer could wait 1 second for the mutex, during which ECG FIFO samples overflow.

- Changing to 50ms timeout reduced `csv_wait_max` from 1000ms to 9ms (P3)

### 4. P3 delivers best result: 130.7 Hz (+75%)

Combined fix:
- `_FS_TIMEOUT=50, _USE_MUTEX=1`
- CSV writer: 50ms mutex timeout
- Audio: 5ms mutex timeout + 2x aggregation
- No periodic f_sync
- `csv_wait_max`: 1000ms ? 9ms
- `csv_hold_max`: 1002ms ? 45ms

### 5. Remaining bottleneck: INTB only 2.2 Hz

In all Case F variants, `notify_wakes` is ~55-65 in 30 seconds = **~2 Hz**, while expected is **374 Hz** (Case A). The MAX30003 INTB/EXTI interrupt is not firing at the expected rate when the full MIC chain is active.

Possible causes (next phase):
- EXTI9_5 priority (7) may be too low vs DMA interrupts (5, 6)
- SDMMC DMA ISRs at priority 5-6 preempting EXTI
- SAI DMA configuration interfering with GPIO EXTI
- MAX30003 SPI bus contention with other SPI devices

---

## Code Changes

### FATFS/Target/ffconf.h
```
_FS_TIMEOUT: 1000 ? 50
_USE_MUTEX:  0    ? 1
```

### Core/Src/multi_sensor_logger.c
```
sd_write_records_checked: osWaitForever ? pdMS_TO_TICKS(50)
file open mutex:          pdMS_TO_TICKS(2000) ? pdMS_TO_TICKS(100)
```

### Core/Inc/record_feature_flags.h
```
RECORD_DIAG_CASE=6 (Case F)
RECORD_FIX_AUDIO_NONBLOCKING_DISCARD=1 (P3 active)
```

---

## Final Assessment

| Item | Status |
|------|--------|
| AudioTask hot-loop reentry | ? Fixed (Case C = 374 Hz) |
| Priority inversion | ? Fixed (Audio ? SensorTask) |
| Case F SD write blocking | ?? Partial: 74.7?130.7 Hz |
| FatFS internal contention | ? Identified + mitigated |
| CSV mutex contention | ? Fixed (9ms max wait) |
| INTB/EXTI degradation | ? Unresolved (2.2Hz vs 374Hz expected) |
| Case A 374?512 Hz | ? Next phase |

### Next Phase Priority
1. Investigate INTB/EXTI degradation in Case F ? check EXTI priority vs DMA ISR preemption
2. Test EXTI priority boost (7?4) 
3. Once INTB recovers, re-test Case F/P3 to confirm approach to 374 Hz
4. Then address Case A 374?512 Hz (FCLK, FIFO burst, etc.)
