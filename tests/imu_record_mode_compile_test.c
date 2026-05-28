#include "imu_record_mode.h"
#include "record_feature_flags.h"

_Static_assert(IMU_RECORD_MODE == IMU_RECORD_MODE_INTERRUPT,
               "IMU recording must be interrupt-driven");
_Static_assert(RECORD_ICM_INIT_RETRY_COUNT >= 3U,
               "ICM20948 init must retry because the shared I2C3/TXS0104 path can miss first WHO_AM_I after reset");

int imu_record_mode_compile_test_anchor;
