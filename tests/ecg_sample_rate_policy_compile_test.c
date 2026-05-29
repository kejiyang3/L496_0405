#include "max3003.h"
#include "multi_sensor_logger.h"
#include "record_feature_flags.h"

#ifndef RECORD_ECG_SAMPLE_RATE_HZ
#error "RECORD_ECG_SAMPLE_RATE_HZ must be defined"
#endif

_Static_assert(RECORD_ECG_SAMPLE_RATE_HZ == 128U,
               "Final fallback validates effective 128 Hz ECG sampling");

_Static_assert(CNFG_ECG_RATE_SELECTED == CNFG_ECG_RATE_256SPS,
               "128 Hz effective fallback uses MAX30003 256 sps hardware mode");

_Static_assert(ECG_SAMPLE_RATE_HZ == RECORD_ECG_SAMPLE_RATE_HZ,
               "Logger ECG rate must follow the recording feature flag");

int main(void)
{
    return 0;
}
