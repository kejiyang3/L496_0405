#include "record_feature_flags.h"

#ifndef RECORD_DEFAULT_RECORD_MS
#error "RECORD_DEFAULT_RECORD_MS must be defined"
#endif

_Static_assert(RECORD_DEFAULT_RECORD_MS == 120000U,
               "Default code-prep mode must be the with-MIC 2 minute smoke test");

int record_duration_policy_compile_test_anchor;
