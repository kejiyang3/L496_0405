#include "record_feature_flags.h"

#ifndef RECORD_MIC_SAMPLE_RATE_HZ
#error "RECORD_MIC_SAMPLE_RATE_HZ must be defined"
#endif

_Static_assert(RECORD_MIC_SAMPLE_RATE_HZ == 8000U,
               "Fallback mode lowers MIC sample rate to 8000 Hz");

int main(void)
{
    return 0;
}
