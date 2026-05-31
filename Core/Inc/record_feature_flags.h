#ifndef RECORD_FEATURE_FLAGS_H
#define RECORD_FEATURE_FLAGS_H

#define RECORD_ENABLE_LVGL 0  /* v0.3: LVGL off */
#define RECORD_ENABLE_PPG 1
#define RECORD_ENABLE_ICM 1
#define RECORD_DEFAULT_RECORD_MS 90000U
#define RECORD_ENABLE_AUDIO 0  /* v0.3: MIC disabled */

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
  #define RECORD_DIAG_ECG_ONLY_MODE       0
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
#define RECORD_FAIL_FAST_ON_MODALITY_ERROR 1
#define RECORD_AUDIO_REOPEN_SAME_SEQ_ALLOWED 0
#define RECORD_AUDIO_ERROR_REQUESTS_STOP 1
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
#define RECORD_TEST_ECG_STATUS_ONLY 0  /* chip-level validation: STATUS-only */
#define RECORD_TEST_ECG_DIAG_ENABLE 1      /* master diag switch */
#define RECORD_ECG_TASK_PRIORITY_BOOST 0   /* P-SW-D: 0=normal, 1=boosted */


/* === Phase 2: C4 Normal vs Burst FIFO Read === */
/* 0=Burst read (default), 1=Normal single-word read, 2=Normal read until ETAG last only */
#define RECORD_TEST_ECG_FIFO_NORMAL_READ 0
/* C4 sub-mode: fixed read size (0=auto 32, 4/8/16/32=fixed words per read) */
#define RECORD_TEST_ECG_FIFO_FIXED_SIZE  0


/* === V3: Lead-off / Fast Recovery test === */
#define RECORD_TEST_MAX30003_NO_DCLOFF  1  /* OLD CODE: no DCLOFF */  /* 1=disable DC lead-off in CNFG_GEN */
#define RECORD_TEST_MAX30003_NO_FASTREC 0  /* 1=disable Fast Recovery in MNGR_DYN */

/* === V4: FMSTR test === */
/* 0=32k(default), 1=16k, 2=8k, 3=4k */
#define RECORD_TEST_MAX30003_FMSTR 0

/* === V6: INTB type === */
#define RECORD_TEST_MAX30003_INTB_CMOS 0  /* 1=CMOS output, 0=Open-Drain */

/* === Phase 2: C5 Rate Select === */
/* 0=512 SPS, 1=256 SPS, 2=128 SPS */
#define RECORD_TEST_ECG_RATE_SELECT 0

/* === Case F SD Write Experiment Flags === */
/* F1: drop before f_write (test if f_write is necessary trigger) */
#define RECORD_TEST_AUDIO_DROP_BEFORE_FWRITE 0
/* F2: f_write only, no WAV header update, no f_sync during recording */
#define RECORD_TEST_AUDIO_WRITE_ONLY_NO_SYNC 0
/* F3: full WAV write but no periodic f_sync/header update */
#define RECORD_TEST_AUDIO_NO_PERIODIC_SYNC 0
/* P3: non-blocking audio discard: 5ms mutex, no f_sync in recording */
#define RECORD_FIX_AUDIO_NONBLOCKING_DISCARD 1

/* === Phase 2: CSV/SD global blocking experiments === */
/* P2: disable CSV writer (MultiSensorLogger_AddECG only counts, no queue) */
#define RECORD_DIAG_DISABLE_CSV_WRITER 0
/* P3-doc: disable SD debug log (SD_DebugLog_* no-ops) */
#define RECORD_DIAG_DISABLE_SD_DEBUG_LOG 0
/* P4: disable ALL SD writes, RAM counting only */
#define RECORD_DIAG_DISABLE_ALL_SD_WRITES 0


/* === P2: Disable PA8 FCLK MCO (test MAX30003 internal RC fallback) === */
#define RECORD_TEST_P2_DISABLE_FCLK_MCO 0


/* v0.3: PPG avg1 for 50 Hz (permanent) */
#define RECORD_TARGET_PPG_AVG1 1

/* === PPG / ICM20948 rate isolation experiments === */
/* 0=normal, 1=PPG-only avg4, 2=PPG-only avg1, 3=ICM-only,
 * 4=ICM-only FIFO/batch probe, 5=PPG+ICM, 6=PPG+ICM no-SD,
 * 7=ECG+PPG+ICM no-MIC. */
#ifndef RECORD_RATE_ISO_CASE
#define RECORD_RATE_ISO_CASE 0
#endif

#define RECORD_RATE_ISO_ENABLE_ECG   ((RECORD_RATE_ISO_CASE) == 0 || (RECORD_RATE_ISO_CASE) == 7)
#define RECORD_RATE_ISO_NO_SD        ((RECORD_RATE_ISO_CASE) == 6)
#define RECORD_RATE_ISO_PPG_AVG1     ((RECORD_RATE_ISO_CASE) == 2)
#define RECORD_RATE_ISO_ICM_BATCH    ((RECORD_RATE_ISO_CASE) == 4)

#if RECORD_RATE_ISO_CASE == 1
  #undef  RECORD_ENABLE_PPG
  #define RECORD_ENABLE_PPG 1
  #undef  RECORD_ENABLE_ICM
  #define RECORD_ENABLE_ICM 0
  #undef  RECORD_ENABLE_AUDIO
  #define RECORD_ENABLE_AUDIO 0
  #undef  RECORD_ENABLE_LVGL
  #define RECORD_ENABLE_LVGL 0
  #define RECORD_RATE_ISO_LABEL "PPG_ONLY_AVG4"
#elif RECORD_RATE_ISO_CASE == 2
  #undef  RECORD_ENABLE_PPG
  #define RECORD_ENABLE_PPG 1
  #undef  RECORD_ENABLE_ICM
  #define RECORD_ENABLE_ICM 0
  #undef  RECORD_ENABLE_AUDIO
  #define RECORD_ENABLE_AUDIO 0
  #undef  RECORD_ENABLE_LVGL
  #define RECORD_ENABLE_LVGL 0
  #define RECORD_RATE_ISO_LABEL "PPG_ONLY_AVG1"
#elif RECORD_RATE_ISO_CASE == 3
  #undef  RECORD_ENABLE_PPG
  #define RECORD_ENABLE_PPG 0
  #undef  RECORD_ENABLE_ICM
  #define RECORD_ENABLE_ICM 1
  #undef  RECORD_ENABLE_AUDIO
  #define RECORD_ENABLE_AUDIO 0
  #undef  RECORD_ENABLE_LVGL
  #define RECORD_ENABLE_LVGL 0
  #define RECORD_RATE_ISO_LABEL "ICM_ONLY"
#elif RECORD_RATE_ISO_CASE == 4
  #undef  RECORD_ENABLE_PPG
  #define RECORD_ENABLE_PPG 0
  #undef  RECORD_ENABLE_ICM
  #define RECORD_ENABLE_ICM 1
  #undef  RECORD_ENABLE_AUDIO
  #define RECORD_ENABLE_AUDIO 0
  #undef  RECORD_ENABLE_LVGL
  #define RECORD_ENABLE_LVGL 0
  #define RECORD_RATE_ISO_LABEL "ICM_ONLY_BATCH"
#elif RECORD_RATE_ISO_CASE == 5
  #undef  RECORD_ENABLE_PPG
  #define RECORD_ENABLE_PPG 1
  #undef  RECORD_ENABLE_ICM
  #define RECORD_ENABLE_ICM 1
  #undef  RECORD_ENABLE_AUDIO
  #define RECORD_ENABLE_AUDIO 0
  #undef  RECORD_ENABLE_LVGL
  #define RECORD_ENABLE_LVGL 0
  #define RECORD_RATE_ISO_LABEL "PPG_ICM"
#elif RECORD_RATE_ISO_CASE == 6
  #undef  RECORD_ENABLE_PPG
  #define RECORD_ENABLE_PPG 1
  #undef  RECORD_ENABLE_ICM
  #define RECORD_ENABLE_ICM 1
  #undef  RECORD_ENABLE_AUDIO
  #define RECORD_ENABLE_AUDIO 0
  #undef  RECORD_ENABLE_LVGL
  #define RECORD_ENABLE_LVGL 0
  #undef  RECORD_DIAG_DISABLE_CSV_WRITER
  #define RECORD_DIAG_DISABLE_CSV_WRITER 1
  #define RECORD_RATE_ISO_LABEL "PPG_ICM_NO_SD"
#elif RECORD_RATE_ISO_CASE == 7
  #undef  RECORD_ENABLE_PPG
  #define RECORD_ENABLE_PPG 1
  #undef  RECORD_ENABLE_ICM
  #define RECORD_ENABLE_ICM 1
  #undef  RECORD_ENABLE_AUDIO
  #define RECORD_ENABLE_AUDIO 0
  #undef  RECORD_ENABLE_LVGL
  #define RECORD_ENABLE_LVGL 0
  #define RECORD_RATE_ISO_LABEL "ECG_PPG_ICM_NO_MIC"
#else
  #define RECORD_RATE_ISO_LABEL "NORMAL"
#endif

/* ===== MIC SAI DMA Stall Experiment Modes ===== */
/* 0=normal, 1=MIC-only no-SD(RAM), 2=MIC-only with-SD, 3=four-modal 90s */
#define RECORD_EXP_MIC_STALL_MODE 3

#if RECORD_EXP_MIC_STALL_MODE == 1
  /* MIC-only, no SD: RAM counting, disable other sensors */
  #undef  RECORD_ENABLE_PPG
  #define RECORD_ENABLE_PPG 0
  #undef  RECORD_ENABLE_ICM
  #define RECORD_ENABLE_ICM 0
  #undef  RECORD_ENABLE_LVGL
  #define RECORD_ENABLE_LVGL 0
  #undef  RECORD_DIAG_DISABLE_ALL_SD_WRITES
  #define RECORD_DIAG_DISABLE_ALL_SD_WRITES 1
  #define RECORD_EXP_LABEL "MIC_NO_SD"
#elif RECORD_EXP_MIC_STALL_MODE == 2
  /* MIC-only, with SD: keep SD writes, disable other sensors */
  #undef  RECORD_ENABLE_PPG
  #define RECORD_ENABLE_PPG 0
  #undef  RECORD_ENABLE_ICM
  #define RECORD_ENABLE_ICM 0
  #undef  RECORD_ENABLE_LVGL
  #define RECORD_ENABLE_LVGL 0
  #define RECORD_EXP_LABEL "MIC_SD"
#elif RECORD_EXP_MIC_STALL_MODE == 3
  /* Four-modal 90s: normal operation */
  #define RECORD_DEFAULT_RECORD_MS 90000U  /* v0.3: 90s default */
  #define RECORD_EXP_LABEL "THREE_MODAL_30MIN"
#else
  /* Normal mode */
  #define RECORD_EXP_LABEL "NORMAL"
#endif

#if RECORD_RATE_ISO_CASE > 0
  #undef  RECORD_DEFAULT_RECORD_MS
  #define RECORD_DEFAULT_RECORD_MS 30000U
#endif

#endif
