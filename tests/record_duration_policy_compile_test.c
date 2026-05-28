#include "record_feature_flags.h"

#ifndef RECORD_DEFAULT_RECORD_MS
#error "RECORD_DEFAULT_RECORD_MS must be defined"
#endif

_Static_assert(RECORD_DEFAULT_RECORD_MS == 30000U,
               "Temporary closed-loop validation duration must be 30 seconds");

int record_duration_policy_compile_test_anchor;
