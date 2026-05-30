#include "multi_sensor_logger.h"

#ifndef MS_ECG_BLOCK_COUNT
#error "MS_ECG_BLOCK_COUNT must be defined"
#endif

_Static_assert(MS_ECG_BLOCK_COUNT >= 4U,
               "ECG logger needs at least four blocks to absorb SD writer stalls");

_Static_assert(MS_PPG_BLOCK_COUNT == 2U,
               "PPG logger should stay double-buffered");

_Static_assert(MS_IMU_BLOCK_COUNT == 2U,
               "IMU logger should stay double-buffered");

int main(void)
{
    return 0;
}
