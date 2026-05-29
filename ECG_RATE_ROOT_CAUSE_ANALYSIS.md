# MAX30003 ECG Rate Degradation: Comprehensive Root Cause Analysis

## Date: 2026-05-29

## Problem Summary
Enabling MIC (Audio Task) causes ECG effective rate to drop from 511 Hz (expected) to ~255 Hz (50% loss).

## Root Cause Confirmed

**The refactored MAX30003_Task() in Core/Src/max3003.c added strict EINT gating that skips FIFO reads when the EINT flag is not set, causing 60% of drain opportunities to be missed.**

### Old Code (HEAD, working):
    if ((status_reg & MAX30003_STATUS_EINT) == 0) {
        words_to_read = MAX30003_NO_EINT_DRAIN_SAMPLES;  // Still reads 32 words!
    }
    // Always burst-reads FIFO, EINT or not

### New Code (refactored, broken):
    if ((status_reg & MAX30003_STATUS_EINT) == 0) {
        return;  // SKIPS FIFO read entirely!
    }
    // Only reads FIFO when EINT is set

## Diagnostic Evidence (MIC ON Run #2, sd_pull_output_mic_on2)

| Metric | Value | Expected |
|--------|-------|----------|
| ECG rate | 255.2 Hz | 512 Hz |
| MAX30003_Task calls | 3800 in 30.15s (126 Hz) | N/A |
| EINT hits | 1539 (40.5%) | ~551 |
| INTB interrupts | 1555 (51.6 Hz) | 18.3 Hz |
| Notification timeouts | 2247 (59%) | ~0 |
| Max inter-call gap | 1002 ms | <10 ms |
| Valid samples/EINT | 5.0 | 28 |
| EOVF events | 15 | 0-1 |

### Burst pattern analysis:
- 1535/1542 bursts = exactly 5 valid samples per burst (99.5%)
- Average inter-burst gap: 18.89 ms
- All samples within a burst share same timestamp
- Typical inter-burst gaps: 9-10ms, occasional spikes >1000ms

## Mechanism

1. MAX30003 at 512 Hz, EFIT=4 (EINT at FIFO count=28)
2. Strict EINT gating: MAX30003_Task returns without drain ~60% of time
3. When EINT IS set (40%), FIFO has only ~5-10 samples
4. Each drain burst extracts 5 valid samples (27 words empty/etag=0x06)
5. 3800 calls x 40% EINT x 5 samples = ~7600 samples in 30s = 255 Hz

## Fix Applied

Reverted Core/Src/max3003.c and Core/Inc/max3003.h to git HEAD (original always-drain behavior).
Diagnostic instrumentation retained for future debugging.

## Verification Needed
1. Format SD card (physically remove and reformat, or add f_mkfs to firmware)
2. Flash firmware with original MAX30003_Task
3. Run 30s MIC ON recording
4. Verify ECG rate = 511 Hz (+/- 5%)

## Key Files
- Core/Src/max3003.c: MAX30003_Task() - FIFO drain logic
- Core/Src/freertos.c: ~line 703 - sensor task loop
- Core/Src/main.c: ~line 365 - ECG INTB ISR callback
- Core/Inc/max3003.h: CNFG_ECG_RATE_512SPS configuration
- Core/Inc/record_feature_flags.h: RECORD_ENABLE_AUDIO, RECORD_ECG_SAMPLE_RATE_HZ
