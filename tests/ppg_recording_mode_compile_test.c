#include "record_feature_flags.h"
#include "max30102.h"

_Static_assert(RECORD_ENABLE_PPG == 1,
               "MAX30102 PPG recording must be enabled");
_Static_assert(RECORD_USE_I2C3_MUTEX == 1,
               "MAX30102 and ICM20948 share I2C3, so runtime reads must be mutex-protected");
_Static_assert(RECORD_PPG_INIT_RETRY_COUNT >= 3U,
               "MAX30102 init must retry because the shared I2C3/TXS0104 path can be dirty after reset");
_Static_assert(RECORD_PPG_FIFO_DRAIN_TIMEOUT_MS <= 1000U,
               "PPG must drain FIFO periodically if the PC2 interrupt edge is absent");
_Static_assert(RECORD_PPG_FIFO_DRAIN_MAX_SAMPLES <= 8U,
               "PPG FIFO drain must stay bounded to avoid starving ECG/ICM");
_Static_assert(RECORD_PPG_SAMPLE_RATE_HZ <= 50U,
               "PPG must use a low-bandwidth stable mode while sharing I2C3 with ICM20948 through TXS0104");
_Static_assert(RECORD_PPG_FIFO_DRAIN_TIMEOUT_MS <= 100U,
               "PPG FIFO drain interval must be short enough to avoid FIFO backlog at low sample rate");
_Static_assert(RECORD_PPG_FIFO_DRAIN_MAX_SAMPLES >= 6U &&
               RECORD_PPG_FIFO_DRAIN_MAX_SAMPLES <= 8U,
               "PPG FIFO drain batch must cover one 100ms window while keeping I2C3 occupancy bounded");
_Static_assert(RECORD_PPG_INT_DRAIN_ROUNDS >= 2U &&
               RECORD_PPG_INT_DRAIN_ROUNDS <= 4U,
               "PPG interrupt path must drain enough FIFO backlog without monopolizing I2C3");
_Static_assert(RECORD_PPG_I2C_TIMEOUT_MS >= 20U &&
               RECORD_PPG_I2C_TIMEOUT_MS <= 30U,
               "PPG I2C timeout must allow an 8-sample FIFO burst through TXS0104 without hiding long bus stalls");
_Static_assert(MAX30102_EXPECTED_SAMPLE_RATE_HZ == RECORD_PPG_SAMPLE_RATE_HZ,
               "MAX30102 register configuration and logger sample-rate metadata must match");
_Static_assert(MAX30102_SPO2_CONFIG_50SPS_18B == 0x22U,
               "MAX30102 stable mode must be 50 SPS, 18-bit pulse width");
_Static_assert(MAX30102_FIFO_CONFIG_STABLE == 0x1FU,
               "v0.3 PPG must use avg1 so 50 Hz produces 50 Hz output");
_Static_assert(MAX30102_INT_ENABLE_RECORDING == 0xC0U,
               "MAX30102 recording must enable A_FULL and PPG_RDY interrupts so PC2 is the primary read trigger");
_Static_assert(MAX30102_FIFO_POINTER_SNAPSHOT_BYTES == 3U,
               "MAX30102 FIFO WR/OV/RD pointers must be read in one contiguous I2C transaction");
_Static_assert(MAX30102_FIFO_OV_COUNTER_MASK == 0x1FU,
               "MAX30102 overflow counter is 5-bit and must not be truncated to 0x0F");

int ppg_recording_mode_compile_test_anchor;
