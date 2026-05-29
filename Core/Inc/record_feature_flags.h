#ifndef RECORD_FEATURE_FLAGS_H
#define RECORD_FEATURE_FLAGS_H

#define RECORD_ENABLE_LVGL 1
#define RECORD_ENABLE_PPG 1
#define RECORD_ENABLE_ICM 1
#define RECORD_DEFAULT_RECORD_MS 30000U
#define RECORD_ENABLE_AUDIO 0  /* P-SW: set 1 to enable full audio */

/* ===== MAX30003 + AudioTask / MIC Conflict Diagnostic ===== */
/* RECORD_DIAG_CASE: 0=normal, 1=CaseA, 2=CaseB, 3=CaseC, 4=CaseD, 5=CaseE, 6=CaseF */
#define RECORD_DIAG_CASE 0

#if RECORD_DIAG_CASE >= 1
  /* Case mode: each case enables exactly one additional layer vs Case A */
  /* A: OFF OFF OFF OFF OFF | B: OFF ON  OFF OFF OFF | C: IDLE OFF OFF OFF OFF */
  /* D: ON  ON  ON  OFF OFF | E: ON  ON  ON  ON  OFF | F: ON  ON  ON  ON  ON  */
  #define RECORD_DIAG_AUDIO_TASK_CREATE   ((RECORD_DIAG_CASE) >= 3)
  #define RECORD_DIAG_AUDIO_EN_MIC_ON     ((RECORD_DIAG_CASE) == 2 || (RECORD_DIAG_CASE) >= 4)
  #define RECORD_DIAG_AUDIO_SAI_DMA       ((RECORD_DIAG_CASE) >= 4)
  #define RECORD_DIAG_AUDIO_MIC_PACK      ((RECORD_DIAG_CASE) >= 5)
  #define RECORD_DIAG_AUDIO_MIC_SD_WRITE  ((RECORD_DIAG_CASE) == 6)
  #define RECORD_DIAG_ECG_ONLY_MODE       1  /* Disable PPG/IMU to reduce noise during diag */
#else
  /* Normal mode: use feature flags as-is */
  #define RECORD_DIAG_AUDIO_TASK_CREATE   (RECORD_ENABLE_AUDIO)
  #define RECORD_DIAG_AUDIO_EN_MIC_ON     (RECORD_ENABLE_AUDIO)
  #define RECORD_DIAG_AUDIO_SAI_DMA       (RECORD_ENABLE_AUDIO)
  #define RECORD_DIAG_AUDIO_MIC_PACK      (RECORD_ENABLE_AUDIO)
  #define RECORD_DIAG_AUDIO_MIC_SD_WRITE  (RECORD_ENABLE_AUDIO)
  #define RECORD_DIAG_ECG_ONLY_MODE       0
#endif

#define RECORD_MIC_SAMPLE_RATE_HZ 8000U
#define RECORD_ECG_SAMPLE_RATE_HZ 512U  /* P-SW-A */
#define RECORD_ISOLATE_AUX_TASKS 1
#define RECORD_USE_I2C3_MUTEX 1
#define RECORD_ICM_INIT_RETRY_COUNT 3U
#define RECORD_PPG_INIT_RETRY_COUNT 3U
#define RECORD_PPG_SAMPLE_RATE_HZ 50U
#define RECORD_PPG_FIFO_DRAIN_TIMEOUT_MS 100U
#define RECORD_PPG_FIFO_DRAIN_MAX_SAMPLES 8U
#define RECORD_PPG_INT_DRAIN_ROUNDS 4U
#define RECORD_PPG_I2C_TIMEOUT_MS 25U
#define RECORD_FAIL_FAST_ON_MODALITY_ERROR 0
#define RECORD_AUDIO_REOPEN_SAME_SEQ_ALLOWED 0
#define RECORD_AUDIO_ERROR_REQUESTS_STOP 0
#define RECORD_REQUIRE_PPG_FOR_FOUR_MODAL 0
#define RECORD_REQUIRE_ICM_FOR_FOUR_MODAL 0
#define RECORD_FAIL_STREAK_RESETS_WHEN_IDLE 1
#define RECORD_FAILED_MODALITY_REINITS_NEXT_START 1
#define RECORD_PPG_FAIL_STREAK_LIMIT 3U
#define RECORD_IMU_FAIL_STREAK_LIMIT 2U

/* === ECG Software Diagnosis Test Macros === */
#define RECORD_DIAG_RUN_INTERVAL_MS 1000U  /* SD diag log interval */
#define RECORD_TEST_ECG_COUNT_ONLY 0       /* P-SW-B off */
#define RECORD_TEST_ECG_FIFO_ONLY 0        /* P-SW-C off */
#define RECORD_TEST_ECG_DIAG_ENABLE 1      /* master diag switch */
#define RECORD_ECG_TASK_PRIORITY_BOOST 0   /* P-SW-D: 0=normal, 1=boosted */

/* === Case F SD Write Experiment Flags === */
/* F1: drop before f_write (test if f_write is necessary trigger) */
#define RECORD_TEST_AUDIO_DROP_BEFORE_FWRITE 0
/* F2: f_write only, no WAV header update, no f_sync during recording */
#define RECORD_TEST_AUDIO_WRITE_ONLY_NO_SYNC 0
/* F3: full WAV write but no periodic f_sync/header update */
#define RECORD_TEST_AUDIO_NO_PERIODIC_SYNC 0
/* P3: non-blocking audio discard: 5ms mutex, no f_sync in recording */
#define RECORD_FIX_AUDIO_NONBLOCKING_DISCARD 0

#endif














