#include "session_summary_fields.h"

_Static_assert(SESSION_SUMMARY_HAS_ECG == 1,
               "session summary must show ECG evidence");
_Static_assert(SESSION_SUMMARY_HAS_PPG == 1,
               "session summary must show PPG evidence");
_Static_assert(SESSION_SUMMARY_HAS_IMU == 1,
               "session summary must show IMU evidence");
_Static_assert(SESSION_SUMMARY_HAS_MIC == 1,
               "session summary must show microphone evidence");
_Static_assert(sizeof(SESSION_SUMMARY_PPG_SAMPLES_KEY) <= 20,
               "PPG session key must stay readable on SD text dumps");

int session_summary_fields_compile_test_anchor;
