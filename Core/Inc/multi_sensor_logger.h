#ifndef __MULTI_SENSOR_LOGGER_H__
#define __MULTI_SENSOR_LOGGER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdint.h>
#include "cmsis_os.h"
#include "record_feature_flags.h"

/* ========== 采样率与 2 秒 Block 容量 ========== */
#define LOG_BLOCK_SECONDS       2

#define ECG_SAMPLE_RATE_HZ      RECORD_ECG_SAMPLE_RATE_HZ
#define PPG_SAMPLE_RATE_HZ      RECORD_PPG_SAMPLE_RATE_HZ
#define IMU_SAMPLE_RATE_HZ      104

#define ECG_BLOCK_SAMPLES       (ECG_SAMPLE_RATE_HZ * LOG_BLOCK_SECONDS)   /* 1024 */
#define PPG_BLOCK_SAMPLES       (PPG_SAMPLE_RATE_HZ * LOG_BLOCK_SECONDS)   /* 400 */
#define IMU_BLOCK_SAMPLES       (IMU_SAMPLE_RATE_HZ * LOG_BLOCK_SECONDS)   /* ~2s @ ICM Data Ready */

#define MS_ECG_BLOCK_COUNT      4U
#define MS_PPG_BLOCK_COUNT      2U
#define MS_IMU_BLOCK_COUNT      2U

/* ========== Block 结构体 ========== */

typedef struct {
    uint32_t timestamp_ms[ECG_BLOCK_SAMPLES];
    uint32_t seq[ECG_BLOCK_SAMPLES];
    int16_t  ecg[ECG_BLOCK_SAMPLES];
    uint16_t count;
} ECG_Block_t;

typedef struct {
    uint32_t timestamp_ms[PPG_BLOCK_SAMPLES];
    uint32_t seq[PPG_BLOCK_SAMPLES];
    uint32_t ir[PPG_BLOCK_SAMPLES];
    uint32_t red[PPG_BLOCK_SAMPLES];
    uint16_t count;
} PPG_Block_t;

typedef struct {
    uint32_t timestamp_ms[IMU_BLOCK_SAMPLES];
    uint32_t seq[IMU_BLOCK_SAMPLES];
    int16_t ax[IMU_BLOCK_SAMPLES];
    int16_t ay[IMU_BLOCK_SAMPLES];
    int16_t az[IMU_BLOCK_SAMPLES];
    int16_t gx[IMU_BLOCK_SAMPLES];
    int16_t gy[IMU_BLOCK_SAMPLES];
    int16_t gz[IMU_BLOCK_SAMPLES];
    uint16_t count;
} IMU_Block_t;

/* ========== Block 消息 (Writer 队列只传描述符) ========== */

typedef enum {
    MS_BLOCK_ECG = 1,
    MS_BLOCK_PPG = 2,
    MS_BLOCK_IMU = 3
} MS_BlockType_t;

typedef struct {
    MS_BlockType_t type;
    uint8_t  block_index;   /* 0 or 1 */
    uint16_t count;         /* 有效样本数 */
} MS_BlockMsg_t;

typedef struct {
    uint32_t ecg_samples;
    uint32_t ecg_write_ok;
    uint32_t ecg_write_fail;
    uint32_t ecg_block_drop;
    uint32_t ppg_samples;
    uint32_t ppg_write_ok;
    uint32_t ppg_write_fail;
    uint32_t ppg_block_drop;
    uint32_t imu_samples;
    uint32_t imu_write_ok;
    uint32_t imu_write_fail;
    uint32_t imu_block_drop;
    uint32_t sd_write_bytes;
    uint32_t sd_sync_count;
    uint32_t writer_get_count;
} MS_Stats_t;

/* ========== 队列 ========== */
extern osMessageQueueId_t Q_MultiSensorBlockHandle;

/* ========== 对外 API ========== */
void MultiSensorLogger_InitQueue(void);
void MultiSensorLogger_ResetForNewRecording(void);
void MultiSensorLogger_RequestStopAndFlush(void);
uint8_t MultiSensorLogger_IsFileOpened(void);
void MultiSensorLogger_GetStats(MS_Stats_t *stats);

/* Add sample 函数 */
void MultiSensorLogger_AddECG(int16_t ecg);
void MultiSensorLogger_AddPPG(uint32_t ir, uint32_t red);
void MultiSensorLogger_AddIMU(int16_t ax, int16_t ay, int16_t az,
                              int16_t gx, int16_t gy, int16_t gz);

/* Writer 任务 */
void StartTask_MultiSensor_SDWriter(void *argument);

#ifdef __cplusplus
}
#endif

#endif /* __MULTI_SENSOR_LOGGER_H__ */
