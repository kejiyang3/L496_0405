#include "record_feature_flags.h"

_Static_assert(RECORD_EXPERIMENT_MODE == RECORD_EXP_WITH_MIC_SMOKE_2MIN,
               "default code-prep firmware must be with-MIC smoke mode");
_Static_assert(RECORD_EXPERIMENT_DURATION_MS == 120000U,
               "with-MIC smoke mode must request 2 minutes");
_Static_assert(RECORD_EXPERIMENT_ENABLE_ECG == 1,
               "with-MIC smoke keeps ECG enabled");
_Static_assert(RECORD_EXPERIMENT_ENABLE_PPG == 1,
               "with-MIC smoke keeps PPG enabled");
_Static_assert(RECORD_EXPERIMENT_ENABLE_ICM == 1,
               "with-MIC smoke keeps ICM enabled");
_Static_assert(RECORD_EXPERIMENT_ENABLE_MIC == 1,
               "with-MIC smoke enables MIC power/task");
_Static_assert(RECORD_EXPERIMENT_ENABLE_WAV == 1,
               "with-MIC smoke writes WAV");
_Static_assert(RECORD_ENABLE_LVGL == 0,
               "v0.3 long-run modes keep LVGL off");

int experiment_mode_policy_compile_test_anchor;
