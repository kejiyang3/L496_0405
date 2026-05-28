#include "multi_sensor_logger.h"

_Static_assert(IMU_SAMPLE_RATE_HZ >= 100,
               "IMU interrupt-driven recording needs the data-ready ODR, not the old polling rate");
_Static_assert(IMU_BLOCK_SAMPLES >= (100 * LOG_BLOCK_SECONDS),
               "IMU block must hold about two seconds of interrupt-driven samples");

int imu_block_capacity_compile_test_anchor;
