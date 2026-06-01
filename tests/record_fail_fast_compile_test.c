#include "record_feature_flags.h"

_Static_assert(RECORD_FAIL_FAST_ON_MODALITY_ERROR == 1,
               "four-modal recording must stop the session when one modality fails");
_Static_assert(RECORD_AUDIO_REOPEN_SAME_SEQ_ALLOWED == 0,
               "audio must not reopen and truncate the same WAV after a runtime write failure");
_Static_assert(RECORD_AUDIO_ERROR_REQUESTS_STOP == 0,
               "MIC must remain a low-priority attached modality and not stop the core session");
_Static_assert(RECORD_REQUIRE_PPG_FOR_FOUR_MODAL == 1,
               "four-modal recording must not start when MAX30102 is not ready");
_Static_assert(RECORD_REQUIRE_ICM_FOR_FOUR_MODAL == 1,
               "four-modal recording must not start when ICM20948 is not ready");
_Static_assert(RECORD_FAIL_STREAK_RESETS_WHEN_IDLE == 1,
               "runtime failure streaks must be reset between recording sessions");
_Static_assert(RECORD_FAILED_MODALITY_REINITS_NEXT_START == 1,
               "a modality that failed at runtime must be reinitialized before the next session");
_Static_assert(RECORD_PPG_FAIL_STREAK_LIMIT > 0U &&
               RECORD_PPG_FAIL_STREAK_LIMIT <= 8U,
               "PPG repeated I2C/FIFO read failures must stop the session quickly");
_Static_assert(RECORD_IMU_FAIL_STREAK_LIMIT > 0U &&
               RECORD_IMU_FAIL_STREAK_LIMIT <= 4U,
               "IMU repeated I2C read failures must stop the session quickly");

int record_fail_fast_compile_test_anchor;
