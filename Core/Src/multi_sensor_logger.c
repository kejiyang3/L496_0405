#include "multi_sensor_logger.h"
#include "ecg_record_control.h"
#include "sd_debug_log.h"
#include "session_manager.h"
#include "fatfs.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

extern osMutexId_t Mtx_SDCardHandle;
extern void Safe_USB_Printf(const char *format, ...);
extern volatile uint32_t g_imu_read_ok_count;
extern volatile uint32_t g_imu_read_fail_count;
extern volatile uint32_t g_max30102_fifo_read_ok_count;
extern volatile uint32_t g_max30102_fifo_read_fail_count;
extern volatile uint32_t g_max30102_fifo_empty_count;
extern volatile uint32_t g_max30102_fifo_ov_count;
extern volatile uint32_t g_ppg_int_wakeup_count;
extern volatile uint32_t g_ppg_timeout_wakeup_count;
extern volatile uint32_t g_ppg_timeout_drain_count;

#define MS_USB_VERBOSE 0

/* ========== 闃熷�?========== */
#define MS_QUEUE_DEPTH      12
osMessageQueueId_t Q_MultiSensorBlockHandle = NULL;

void MultiSensorLogger_InitQueue(void)
{
    if (Q_MultiSensorBlockHandle == NULL) {
        Q_MultiSensorBlockHandle = osMessageQueueNew(
            MS_QUEUE_DEPTH, sizeof(MS_BlockMsg_t), NULL);
    }
}

/* ========== 鍙岀紦鍐?========== */
static ECG_Block_t s_ecg_blocks[MS_ECG_BLOCK_COUNT];
static PPG_Block_t s_ppg_blocks[MS_PPG_BLOCK_COUNT];
static IMU_Block_t s_imu_blocks[MS_IMU_BLOCK_COUNT];

static uint8_t s_ecg_active = 0;
static uint8_t s_ppg_active = 0;
static uint8_t s_imu_active = 0;

static volatile uint8_t s_ecg_block_free[MS_ECG_BLOCK_COUNT] = {1, 1, 1, 1};
static volatile uint8_t s_ppg_block_free[MS_PPG_BLOCK_COUNT] = {1, 1};
static volatile uint8_t s_imu_block_free[MS_IMU_BLOCK_COUNT] = {1, 1};

static uint32_t s_ecg_seq = 0;
static uint32_t s_ppg_seq = 0;
static uint32_t s_imu_seq = 0;

/* 缁熻�?*/
static volatile uint32_t s_ppg_sample_count = 0;
static volatile uint32_t s_imu_sample_count = 0;
static volatile uint32_t s_ecg_block_drop = 0;
static volatile uint32_t s_ppg_block_drop = 0;
static volatile uint32_t s_imu_block_drop = 0;

/* 鐪熷疄鍐欏叆鎴愬�?澶辫触璁℃暟 */
static volatile uint32_t s_ecg_write_ok  = 0;
static volatile uint32_t s_ecg_write_fail = 0;
static volatile uint32_t s_ppg_write_ok  = 0;
static volatile uint32_t s_ppg_write_fail = 0;
static volatile uint32_t s_imu_write_ok  = 0;
static volatile uint32_t s_imu_write_fail = 0;
static volatile uint32_t s_ecg_submit_ok = 0;
static volatile uint32_t s_ecg_submit_fail = 0;
static volatile uint32_t s_writer_get_count = 0;

/* stop 璇锋眰鏍囧織 */
static volatile uint8_t s_stop_requested = 0;

/* 鏂囦欢鐘舵€?*/
static FIL s_ms_file;
static volatile uint8_t s_file_opened = 0;

/* ========== 鍐呴儴锛氭彁�?block ========== */
static void submit_ecg_block(uint16_t count)
{
    uint8_t idx = s_ecg_active;
    uint8_t next = (uint8_t)((idx + 1U) % MS_ECG_BLOCK_COUNT);
    MS_BlockMsg_t msg;

    if (!s_ecg_block_free[next]) {
        s_ecg_block_drop++;
    g_ecg_rec.ecg_pack_drop_blocks++;
        s_ecg_submit_fail++;
        Safe_USB_Printf("[MS_SUBMIT][ERR] next busy idx=%u next=%u count=%u drop=%lu\r\n",
                        idx, next, count, (unsigned long)s_ecg_block_drop);
        s_ecg_blocks[idx].count = 0;
        return;
    }

    msg.type = MS_BLOCK_ECG;
    msg.block_index = idx;
    msg.count = count;

    if (osMessageQueuePut(Q_MultiSensorBlockHandle, &msg, 0, 0) == osOK) {
        s_ecg_active = next;
        s_ecg_block_free[idx] = 0;
        s_ecg_submit_ok++;
        g_ecg_rec.ecg_queue_submit_ok++;
#if MS_USB_VERBOSE
        Safe_USB_Printf("[MS_SUBMIT] ECG ok idx=%u count=%u q=%lu ok=%lu\r\n",
                        idx, count,
                        (unsigned long)osMessageQueueGetCount(Q_MultiSensorBlockHandle),
                        (unsigned long)s_ecg_submit_ok);
#endif
    } else {
        s_ecg_block_drop++;
        s_ecg_submit_fail++;
        g_ecg_rec.ecg_queue_submit_fail++;
        Safe_USB_Printf("[MS_SUBMIT][ERR] queue put fail idx=%u count=%u q=%lu fail=%lu\r\n",
                        idx, count,
                        (unsigned long)osMessageQueueGetCount(Q_MultiSensorBlockHandle),
                        (unsigned long)s_ecg_submit_fail);
        s_ecg_blocks[idx].count = 0;
    }
}

static void submit_ppg_block(uint16_t count)
{
    uint8_t idx = s_ppg_active;
    uint8_t next = (uint8_t)((idx + 1U) % MS_PPG_BLOCK_COUNT);
    MS_BlockMsg_t msg;

    if (!s_ppg_block_free[next]) {
        s_ppg_block_drop++;
        s_ppg_blocks[idx].count = 0;
        return;
    }

    msg.type = MS_BLOCK_PPG;
    msg.block_index = idx;
    msg.count = count;

    if (osMessageQueuePut(Q_MultiSensorBlockHandle, &msg, 0, 0) == osOK) {
        s_ppg_active = next;
        s_ppg_block_free[idx] = 0;
    } else {
        s_ppg_block_drop++;
        s_ppg_blocks[idx].count = 0;
    }
}

static void submit_imu_block(uint16_t count)
{
    uint8_t idx = s_imu_active;
    uint8_t next = (uint8_t)((idx + 1U) % MS_IMU_BLOCK_COUNT);
    MS_BlockMsg_t msg;

    if (!s_imu_block_free[next]) {
        s_imu_block_drop++;
        s_imu_blocks[idx].count = 0;
        return;
    }

    msg.type = MS_BLOCK_IMU;
    msg.block_index = idx;
    msg.count = count;

    if (osMessageQueuePut(Q_MultiSensorBlockHandle, &msg, 0, 0) == osOK) {
        s_imu_active = next;
        s_imu_block_free[idx] = 0;
    } else {
        s_imu_block_drop++;
        s_imu_blocks[idx].count = 0;
    }
}

/* ========== Add Sample ========== */

void MultiSensorLogger_AddECG(int16_t ecg)
{
#if RECORD_DIAG_DISABLE_CSV_WRITER || RECORD_DIAG_DISABLE_ALL_SD_WRITES
    /* P2/P4: disable CSV writer ? only count, no queue */
    g_ecg_rec.ecg_sample_count++;
    return;
#else
    if (g_ecg_rec.state != ECG_REC_RECORDING) return;
#endif

    uint8_t idx = s_ecg_active;
    ECG_Block_t *blk = &s_ecg_blocks[idx];

    uint16_t pos = blk->count;
    blk->timestamp_ms[pos] = HAL_GetTick();
    blk->seq[pos] = s_ecg_seq++;
    blk->ecg[pos] = ecg;
    blk->count = pos + 1;

    g_ecg_rec.ecg_sample_count++;

    if (blk->count >= ECG_BLOCK_SAMPLES) {
        submit_ecg_block(blk->count);
        g_ecg_rec.ecg_pack_blocks++;
    }
}

void MultiSensorLogger_AddPPG(uint32_t ir, uint32_t red)
{
    if (g_ecg_rec.state != ECG_REC_RECORDING) return;

    uint8_t idx = s_ppg_active;
    PPG_Block_t *blk = &s_ppg_blocks[idx];

    uint16_t pos = blk->count;
    blk->timestamp_ms[pos] = HAL_GetTick();
    blk->seq[pos] = s_ppg_seq++;
    blk->ir[pos] = ir;
    blk->red[pos] = red;
    blk->count = pos + 1;

    s_ppg_sample_count++;

    if (blk->count >= PPG_BLOCK_SAMPLES) {
        submit_ppg_block(blk->count);
    }
}

void MultiSensorLogger_AddIMU(int16_t ax, int16_t ay, int16_t az,
                              int16_t gx, int16_t gy, int16_t gz)
{
    if (g_ecg_rec.state != ECG_REC_RECORDING) return;

    uint8_t idx = s_imu_active;
    IMU_Block_t *blk = &s_imu_blocks[idx];

    uint16_t pos = blk->count;
    blk->timestamp_ms[pos] = HAL_GetTick();
    blk->seq[pos] = s_imu_seq++;
    blk->ax[pos] = ax;
    blk->ay[pos] = ay;
    blk->az[pos] = az;
    blk->gx[pos] = gx;
    blk->gy[pos] = gy;
    blk->gz[pos] = gz;
    blk->count = pos + 1;

    s_imu_sample_count++;

    if (blk->count >= IMU_BLOCK_SAMPLES) {
        submit_imu_block(blk->count);
    }
}

/* ========== Stop �?flush 鍗婃�?block ========== */

void MultiSensorLogger_RequestStopAndFlush(void)
{
    if (s_ecg_blocks[s_ecg_active].count > 0) {
        submit_ecg_block(s_ecg_blocks[s_ecg_active].count);
    }
    if (s_ppg_blocks[s_ppg_active].count > 0) {
        submit_ppg_block(s_ppg_blocks[s_ppg_active].count);
    }
    if (s_imu_blocks[s_imu_active].count > 0) {
        submit_imu_block(s_imu_blocks[s_imu_active].count);
    }

    s_stop_requested = 1;
}

void MultiSensorLogger_ResetForNewRecording(void)
{
    s_stop_requested = 0;
    s_ecg_seq = 0;
    s_ppg_seq = 0;
    s_imu_seq = 0;

    s_ppg_sample_count = 0;
    s_imu_sample_count = 0;
    s_ecg_block_drop = 0;
    s_ppg_block_drop = 0;
    s_imu_block_drop = 0;

    s_ecg_write_ok   = 0;
    s_ecg_write_fail = 0;
    s_ppg_write_ok   = 0;
    s_ppg_write_fail = 0;
    s_imu_write_ok   = 0;
    s_imu_write_fail = 0;
    s_ecg_submit_ok = 0;
    s_ecg_submit_fail = 0;
    s_writer_get_count = 0;

    for (uint8_t i = 0; i < MS_ECG_BLOCK_COUNT; i++) {
        s_ecg_blocks[i].count = 0;
        s_ecg_block_free[i] = 1;
    }
    for (uint8_t i = 0; i < MS_PPG_BLOCK_COUNT; i++) {
        s_ppg_blocks[i].count = 0;
        s_ppg_block_free[i] = 1;
    }
    for (uint8_t i = 0; i < MS_IMU_BLOCK_COUNT; i++) {
        s_imu_blocks[i].count = 0;
        s_imu_block_free[i] = 1;
    }
    s_ecg_active = 0;
    s_ppg_active = 0;
    s_imu_active = 0;

    g_ecg_rec.ecg_sample_count = 0;
    g_ecg_rec.ecg_written_count = 0;
    g_ecg_rec.ecg_drop_count = 0;
    g_ecg_rec.sd_write_bytes = 0;
    g_ecg_rec.sd_sync_count = 0;
    g_ecg_rec.sd_file_opened = 0;
    g_ecg_rec.sd_file_closed = 1;
    g_ecg_rec.start_tick = 0;
    g_ecg_rec.stop_tick = 0;
    g_ecg_rec.sync_epoch_tick = HAL_GetTick();
    g_ecg_rec.ecg_stream_start_tick = 0;
    g_ecg_rec.ecg_stream_stop_tick = 0;
    g_ecg_rec.mic_power_tick = 0;
    g_ecg_rec.mic_file_open_tick = 0;
    g_ecg_rec.mic_dma_start_tick = 0;
    g_ecg_rec.mic_first_half_tick = 0;
    g_ecg_rec.mic_stop_tick = 0;
    g_ecg_rec.mic_bytes = 0;
    g_ecg_rec.mic_halves = 0;
    g_ecg_rec.mic_drops = 0;
    g_ecg_rec.mic_write_errors = 0;
    s_file_opened = 0;
}

uint8_t MultiSensorLogger_IsFileOpened(void)
{
    return s_file_opened;
}

void MultiSensorLogger_GetStats(MS_Stats_t *stats)
{
    if (stats == NULL) {
        return;
    }

    stats->ecg_samples = g_ecg_rec.ecg_sample_count;
    stats->ecg_write_ok = s_ecg_write_ok;
    stats->ecg_write_fail = s_ecg_write_fail;
    stats->ecg_block_drop = s_ecg_block_drop;
    stats->ppg_samples = s_ppg_sample_count;
    stats->ppg_write_ok = s_ppg_write_ok;
    stats->ppg_write_fail = s_ppg_write_fail;
    stats->ppg_block_drop = s_ppg_block_drop;
    stats->imu_samples = s_imu_sample_count;
    stats->imu_write_ok = s_imu_write_ok;
    stats->imu_write_fail = s_imu_write_fail;
    stats->imu_block_drop = s_imu_block_drop;
    stats->sd_write_bytes = g_ecg_rec.sd_write_bytes;
    stats->sd_sync_count = g_ecg_rec.sd_sync_count;
    stats->writer_get_count = s_writer_get_count;
    stats->ecg_submit_ok = s_ecg_submit_ok;
    stats->ecg_submit_fail = s_ecg_submit_fail;
}

/* ========== Writer: block 閲婃�?========== */
static void free_ecg_block(uint8_t idx) { s_ecg_blocks[idx].count = 0; s_ecg_block_free[idx] = 1; }
static void free_ppg_block(uint8_t idx) { s_ppg_blocks[idx].count = 0; s_ppg_block_free[idx] = 1; }
static void free_imu_block(uint8_t idx) { s_imu_blocks[idx].count = 0; s_imu_block_free[idx] = 1; }

/* ========== checked f_write helper ========== */
static inline int sd_write_records_checked(FIL *fp, const char *buf, UINT len,
                                           uint16_t records,
                                           volatile uint32_t *ok,
                                           volatile uint32_t *fail,
                                           volatile uint32_t *bytes_out)
{
    UINT bw = 0;
    FRESULT res;

    uint32_t sd_mtx_t0 = HAL_GetTick();
    g_sd_csv_diag.acquire_count++;
    if (Mtx_SDCardHandle != NULL) {
        if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(50)) != osOK) {
            g_sd_csv_diag.acquire_timeout++;
            if (bytes_out) *bytes_out = 0;
            return 0;
        }
        uint32_t wms = HAL_GetTick() - sd_mtx_t0;
        g_sd_csv_diag.wait_ms_last = wms;
        if (wms > g_sd_csv_diag.wait_ms_max) g_sd_csv_diag.wait_ms_max = wms;
    }
    {
        uint32_t twr = HAL_GetTick();
        res = f_write(fp, buf, len, &bw);
        uint32_t wrms = HAL_GetTick() - twr;
        g_sd_csv_diag.write_ms_last = wrms;
        if (wrms > g_sd_csv_diag.write_ms_max) g_sd_csv_diag.write_ms_max = wrms;
    }
    {
        uint32_t hms = HAL_GetTick() - sd_mtx_t0;
        g_sd_csv_diag.hold_ms_last = hms;
        if (hms > g_sd_csv_diag.hold_ms_max) g_sd_csv_diag.hold_ms_max = hms;
    }
    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }

    if (res == FR_OK && bw == len) {
        (*ok) += records;
        *bytes_out += bw;
        return 1;
    }
    (*fail) += records;
    return 0;
}

/* ========== Writer: CSV 鍐欏叆鍑芥暟 ========== */

static void write_ecg_block(FIL *fp, uint8_t idx)
{
    ECG_Block_t *blk = &s_ecg_blocks[idx];
    char line[64];
    char chunk[512];
    UINT used = 0;
    uint16_t chunk_records = 0;

    if (blk->count > 0) {
        char blkhdr[48];
        int hn = snprintf(blkhdr, sizeof(blkhdr),
            "BLOCK,%lu,%lu,%u\r\n",
            (unsigned long)blk->timestamp_ms[0],
            (unsigned long)blk->timestamp_ms[blk->count - 1],
            (unsigned)blk->count);
        if (hn > 0 && hn < (int)sizeof(blkhdr)) {
            UINT hbw;
            f_write(fp, blkhdr, (UINT)hn, &hbw);
        }
    }

    /* BLOCK header: tick_start_ms, tick_end_ms, sample_count */
    if (blk->count > 0) {
        char blkhdr[48];
        int hn = snprintf(blkhdr, sizeof(blkhdr),
            "BLOCK,%lu,%lu,%u\r\n",
            (unsigned long)blk->timestamp_ms[0],
            (unsigned long)blk->timestamp_ms[blk->count - 1],
            (unsigned)blk->count);
        if (hn > 0 && hn < (int)sizeof(blkhdr)) {
            UINT hbw;
            f_write(fp, blkhdr, (UINT)hn, &hbw);
        }
    }

    for (uint16_t i = 0; i < blk->count; i++) {
        int n = snprintf(line, sizeof(line),
            "%lu,ECG,%lu,%d,0,0,0,0,0\r\n",
            blk->timestamp_ms[i], blk->seq[i], blk->ecg[i]);
        if (n > 0 && n < (int)sizeof(line)) {
            if ((used + (UINT)n) > sizeof(chunk) && used > 0U) {
                if (sd_write_records_checked(fp, chunk, used, chunk_records,
                                             &s_ecg_write_ok, &s_ecg_write_fail,
                                             &g_ecg_rec.sd_write_bytes)) {
                    g_ecg_rec.ecg_written_count += chunk_records;
                }
                used = 0;
                chunk_records = 0;
            }
            memcpy(&chunk[used], line, (size_t)n);
            used += (UINT)n;
            chunk_records++;
        }
    }
    if (used > 0U) {
        if (sd_write_records_checked(fp, chunk, used, chunk_records,
                                     &s_ecg_write_ok, &s_ecg_write_fail,
                                     &g_ecg_rec.sd_write_bytes)) {
            g_ecg_rec.ecg_written_count += chunk_records;
        }
    }
    free_ecg_block(idx);
}

static void write_ppg_block(FIL *fp, uint8_t idx)
{
    PPG_Block_t *blk = &s_ppg_blocks[idx];
    char line[64];
    char chunk[512];
    UINT used = 0;
    uint16_t chunk_records = 0;

    for (uint16_t i = 0; i < blk->count; i++) {
        int n = snprintf(line, sizeof(line),
            "%lu,PPG,%lu,%lu,%lu,0,0,0,0\r\n",
            blk->timestamp_ms[i], blk->seq[i], blk->ir[i], blk->red[i]);
        if (n > 0 && n < (int)sizeof(line)) {
            if ((used + (UINT)n) > sizeof(chunk) && used > 0U) {
                sd_write_records_checked(fp, chunk, used, chunk_records,
                                         &s_ppg_write_ok, &s_ppg_write_fail,
                                         &g_ecg_rec.sd_write_bytes);
                used = 0;
                chunk_records = 0;
            }
            memcpy(&chunk[used], line, (size_t)n);
            used += (UINT)n;
            chunk_records++;
        }
    }
    if (used > 0U) {
        sd_write_records_checked(fp, chunk, used, chunk_records,
                                 &s_ppg_write_ok, &s_ppg_write_fail,
                                 &g_ecg_rec.sd_write_bytes);
    }
    free_ppg_block(idx);
}

static void write_imu_block(FIL *fp, uint8_t idx)
{
    IMU_Block_t *blk = &s_imu_blocks[idx];
    char line[72];
    char chunk[512];
    UINT used = 0;
    uint16_t chunk_records = 0;

    for (uint16_t i = 0; i < blk->count; i++) {
        int n = snprintf(line, sizeof(line),
            "%lu,IMU,%lu,%d,%d,%d,%d,%d,%d\r\n",
            blk->timestamp_ms[i], blk->seq[i],
            blk->ax[i], blk->ay[i], blk->az[i],
            blk->gx[i], blk->gy[i], blk->gz[i]);
        if (n > 0 && n < (int)sizeof(line)) {
            if ((used + (UINT)n) > sizeof(chunk) && used > 0U) {
                sd_write_records_checked(fp, chunk, used, chunk_records,
                                         &s_imu_write_ok, &s_imu_write_fail,
                                         &g_ecg_rec.sd_write_bytes);
                used = 0;
                chunk_records = 0;
            }
            memcpy(&chunk[used], line, (size_t)n);
            used += (UINT)n;
            chunk_records++;
        }
    }
    if (used > 0U) {
        sd_write_records_checked(fp, chunk, used, chunk_records,
                                 &s_imu_write_ok, &s_imu_write_fail,
                                 &g_ecg_rec.sd_write_bytes);
    }
    free_imu_block(idx);
}

/* ========== Writer 浠诲�?========== */

#define MS_SYNC_EVERY_BLOCKS    8

void StartTask_MultiSensor_SDWriter(void *argument)
{
    (void)argument;
    MS_BlockMsg_t msg;
    FRESULT res;
    uint32_t total_blocks = 0;

    MultiSensorLogger_InitQueue();

    for (;;) {
        /* 绛夊�?RECORDING 鐘舵�?*/
        while (g_ecg_rec.state != ECG_REC_RECORDING) {
            osDelay(50);
        }

        s_stop_requested = 0;
        s_file_opened = 0;
        total_blocks = 0;

        /* 鎵撳紑鏂囦欢 */
        if (Mtx_SDCardHandle != NULL) {
            if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(100)) != osOK) {
                Safe_USB_Printf("[MS_SD][ERR] mutex timeout before open\r\n");
                g_ecg_rec.state = ECG_REC_ERROR;
                continue;
            }
        }

        Safe_USB_Printf("[MS_SD] mount begin session=%s\r\n", g_session.session_id);
        res = f_mount(&SDFatFS, SDPath, 1);
        if (res != FR_OK) {
            Safe_USB_Printf("[MS_SD][ERR] mount res=%d\r\n", res);
            SD_DebugLog_WriteLine("MS_WRITER_MOUNT_FAIL");
            if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
            g_ecg_rec.state = ECG_REC_ERROR;
            continue;
        }
        Safe_USB_Printf("[MS_SD] mount ok\r\n");
        Session_Create(HAL_GetTick());

        Safe_USB_Printf("[MS_SD] open begin file=%s\r\n", g_ecg_rec.file_name);
                { char fpath[64]; snprintf(fpath, sizeof(fpath), "%s/ecg_samples.csv", g_session.session_dir);
          res = f_open(&s_ms_file, fpath, FA_CREATE_ALWAYS | FA_WRITE); }
        if (res != FR_OK) {
            Safe_USB_Printf("[MS_SD][ERR] open res=%d\r\n", res);
            SD_DebugLog_WriteLine("MS_WRITER_OPEN_FAIL");
            if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
            g_ecg_rec.state = ECG_REC_ERROR;
            continue;
        }
        Safe_USB_Printf("[MS_SD] open ok\r\n");

        /* 鍐欒〃澶?(妫€鏌ョ粨鏋? */
        {
            const char *header = "timestamp_ms,type,seq,v1,v2,v3,v4,v5,v6\r\n";
            UINT hdr_len = (UINT)strlen(header);
            UINT hdr_bw = 0;
            res = f_write(&s_ms_file, header, hdr_len, &hdr_bw);
            if (res != FR_OK || hdr_bw != hdr_len) {
                Safe_USB_Printf("[MS_SD][ERR] header res=%d bw=%lu len=%lu\r\n",
                                res, (unsigned long)hdr_bw, (unsigned long)hdr_len);
                SD_DebugLog_WriteLine("MS_WRITER_HEADER_FAIL");
                f_close(&s_ms_file);
                if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
                g_ecg_rec.state = ECG_REC_ERROR;
                continue;
            }
            g_ecg_rec.sd_write_bytes += hdr_bw;
            Safe_USB_Printf("[MS_SD] header ok bw=%lu\r\n", (unsigned long)hdr_bw);
            {
                char meta[96];
                int n = snprintf(meta, sizeof(meta),
                    "%lu,META_START,%lu,%lu,0,0,0,0,0\r\n",
                    (unsigned long)HAL_GetTick(),
                    (unsigned long)g_ecg_rec.file_seq,
                    (unsigned long)g_ecg_rec.sync_epoch_tick);
                if (n > 0 && n < (int)sizeof(meta)) {
                    UINT meta_bw = 0;
                    res = f_write(&s_ms_file, meta, (UINT)n, &meta_bw);
                    if (res == FR_OK && meta_bw == (UINT)n) {
                        g_ecg_rec.sd_write_bytes += meta_bw;
                    }
                }
            }
        }
        res = f_sync(&s_ms_file);
        g_ecg_rec.sd_sync_count++;
        Safe_USB_Printf("[MS_SD] first sync res=%d\r\n", res);

        s_file_opened = 1;
        g_ecg_rec.sd_file_opened = 1;
        g_ecg_rec.sd_file_closed = 0;

        if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);

        Safe_USB_Printf("[MS_SD] file opened and released\r\n");

        /* 涓诲惊鐜細�?block 鍐欏�?*/
        while (g_ecg_rec.state == ECG_REC_RECORDING ||
               g_ecg_rec.state == ECG_REC_STOPPING ||
               osMessageQueueGetCount(Q_MultiSensorBlockHandle) > 0) {

            if (osMessageQueueGet(Q_MultiSensorBlockHandle, &msg, NULL,
                                  pdMS_TO_TICKS(50)) == osOK) {
                s_writer_get_count++;
                g_ecg_rec.ecg_writer_get_blocks++;
#if MS_USB_VERBOSE
                Safe_USB_Printf("[MS_WRITER] got type=%u idx=%u count=%u q=%lu got=%lu\r\n",
                                (unsigned int)msg.type,
                                (unsigned int)msg.block_index,
                                (unsigned int)msg.count,
                                (unsigned long)osMessageQueueGetCount(Q_MultiSensorBlockHandle),
                                (unsigned long)s_writer_get_count);
#endif
                switch (msg.type) {
                case MS_BLOCK_ECG:
                    write_ecg_block(&s_ms_file, msg.block_index);
                    break;
                case MS_BLOCK_PPG:
                    write_ppg_block(&s_ms_file, msg.block_index);
                    break;
                case MS_BLOCK_IMU:
                    write_imu_block(&s_ms_file, msg.block_index);
                    break;
                default:
                    break;
                }
                total_blocks++;
#if MS_USB_VERBOSE
                Safe_USB_Printf("[MS_WRITER] wrote type=%u total=%lu bytes=%lu ecg_written=%lu fail=%lu\r\n",
                                (unsigned int)msg.type,
                                (unsigned long)total_blocks,
                                (unsigned long)g_ecg_rec.sd_write_bytes,
                                (unsigned long)g_ecg_rec.ecg_written_count,
                                (unsigned long)s_ecg_write_fail);
#endif

                if ((total_blocks % MS_SYNC_EVERY_BLOCKS) == 0) {
                    FRESULT sync_res;
                    if (Mtx_SDCardHandle != NULL) {
                        if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(20)) != osOK) {
                            s_ecg_write_fail++;
                            continue;
                        }
                    }
                    {
                        uint32_t csv_s = HAL_GetTick();
                        sync_res = f_sync(&s_ms_file);
                        uint32_t csv_sms = HAL_GetTick() - csv_s;
                        g_sd_csv_diag.sync_ms_last = csv_sms;
                        if (csv_sms > g_sd_csv_diag.sync_ms_max) g_sd_csv_diag.sync_ms_max = csv_sms;
                    }
                    if (Mtx_SDCardHandle != NULL) {
                        osMutexRelease(Mtx_SDCardHandle);
                    }
                    g_ecg_rec.sd_sync_count++;
                    (void)sync_res;
#if MS_USB_VERBOSE
                    Safe_USB_Printf("[MS_SD] sync blocks=%lu res=%d bytes=%lu ecg_written=%lu\r\n",
                                    (unsigned long)total_blocks, sync_res,
                                    (unsigned long)g_ecg_rec.sd_write_bytes,
                                    (unsigned long)g_ecg_rec.ecg_written_count);
#endif
                }

            }

            if (s_stop_requested &&
                osMessageQueueGetCount(Q_MultiSensorBlockHandle) == 0) {
                break;
            }
        }

        /* 鍏抽棴鏂囦欢 �?�?fsync+close锛屼�?unmount */
        FRESULT final_sync = FR_OK;
        FRESULT close_res = FR_OK;
        {
            int close_mutex_ok = 1;
            if (Mtx_SDCardHandle != NULL) {
                if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(100)) != osOK) {
                    close_mutex_ok = 0;
                }
            }
            if (close_mutex_ok) {
                uint32_t csv_s = HAL_GetTick();
                final_sync = f_sync(&s_ms_file);
                uint32_t csv_sms = HAL_GetTick() - csv_s;
                g_sd_csv_diag.sync_ms_last = csv_sms;
                if (csv_sms > g_sd_csv_diag.sync_ms_max) g_sd_csv_diag.sync_ms_max = csv_sms;
                close_res = f_close(&s_ms_file);
                if (Mtx_SDCardHandle != NULL) {
                    osMutexRelease(Mtx_SDCardHandle);
                }
            }
        }
        Safe_USB_Printf("[MS_SD] close final_sync=%d close=%d bytes=%lu ecg_written=%lu\r\n",
                        final_sync, close_res,
                        (unsigned long)g_ecg_rec.sd_write_bytes,
                        (unsigned long)g_ecg_rec.ecg_written_count);
        /* 涓嶈皟鐢?f_mount(NULL)锛岄伩鍏嶅奖�?PPGDiagWriter 绛夋寔鏈夋枃浠剁殑浠诲姟 */

        s_file_opened = 0;
        g_ecg_rec.sd_file_opened = 0;
        g_ecg_rec.sd_file_closed = 1;
        g_ecg_rec.stop_tick = HAL_GetTick();
        g_ecg_rec.state = ECG_REC_STOPPED;

        /* 鍐欑粺璁℃憳瑕佸�?debug_log */
        #if 1
        {
            char stats[640];
            int n = snprintf(stats, sizeof(stats),
                "MULTI_STATS,"
                "seq=%lu,epoch=%lu,ecg_start=%lu,ecg_stop=%lu,"
                "mic_dma_start=%lu,mic_first_half=%lu,mic_stop=%lu,"
                "ecg_samples=%lu,ecg_write_ok=%lu,ecg_write_fail=%lu,"
                "ppg_samples=%lu,ppg_write_ok=%lu,ppg_write_fail=%lu,"
                "imu_samples=%lu,imu_write_ok=%lu,imu_write_fail=%lu,"
                "ecg_drop_blk=%lu,ppg_drop_blk=%lu,imu_drop_blk=%lu,"
                "sd_bytes=%lu,sync_count=%lu,mic_bytes=%lu,mic_halves=%lu,mic_drops=%lu,mic_write_errors=%lu",
                (unsigned long)g_ecg_rec.file_seq,
                (unsigned long)g_ecg_rec.sync_epoch_tick,
                (unsigned long)g_ecg_rec.ecg_stream_start_tick,
                (unsigned long)g_ecg_rec.ecg_stream_stop_tick,
                (unsigned long)g_ecg_rec.mic_dma_start_tick,
                (unsigned long)g_ecg_rec.mic_first_half_tick,
                (unsigned long)g_ecg_rec.mic_stop_tick,
                (unsigned long)g_ecg_rec.ecg_written_count,
                (unsigned long)s_ecg_write_ok,
                (unsigned long)s_ecg_write_fail,
                (unsigned long)s_ppg_sample_count,
                (unsigned long)s_ppg_write_ok,
                (unsigned long)s_ppg_write_fail,
                (unsigned long)s_imu_sample_count,
                (unsigned long)s_imu_write_ok,
                (unsigned long)s_imu_write_fail,
                (unsigned long)s_ecg_block_drop,
                (unsigned long)s_ppg_block_drop,
                (unsigned long)s_imu_block_drop,
                (unsigned long)g_ecg_rec.sd_write_bytes,
                (unsigned long)g_ecg_rec.sd_sync_count,
                (unsigned long)g_ecg_rec.mic_bytes,
                (unsigned long)g_ecg_rec.mic_halves,
                (unsigned long)g_ecg_rec.mic_drops,
                (unsigned long)g_ecg_rec.mic_write_errors);
            if (n > 0 && n < (int)sizeof(stats)) {
                SD_DebugLog_WriteLine(stats);
            }
        }
        SD_DebugLog_WriteLine("MULTI_SENSOR_RECORD_STOPPED");
        {
            char ppg_stats[240];
            int n = snprintf(ppg_stats, sizeof(ppg_stats),
                "PPG_STATS,intwake=%lu,towake=%lu,todrain=%lu,read_ok=%lu,read_fail=%lu,empty=%lu,fifo_ov=%lu,samples=%lu,write_ok=%lu,write_fail=%lu,drop_blk=%lu",
                (unsigned long)g_ppg_int_wakeup_count,
                (unsigned long)g_ppg_timeout_wakeup_count,
                (unsigned long)g_ppg_timeout_drain_count,
                (unsigned long)g_max30102_fifo_read_ok_count,
                (unsigned long)g_max30102_fifo_read_fail_count,
                (unsigned long)g_max30102_fifo_empty_count,
                (unsigned long)g_max30102_fifo_ov_count,
                (unsigned long)s_ppg_sample_count,
                (unsigned long)s_ppg_write_ok,
                (unsigned long)s_ppg_write_fail,
                (unsigned long)s_ppg_block_drop);
            if (n > 0 && n < (int)sizeof(ppg_stats)) {
                SD_DebugLog_WriteLine(ppg_stats);
            }
        }
        {
            char imu_stats[128];
            int n = snprintf(imu_stats, sizeof(imu_stats),
                "IMU_STATS,read_ok=%lu,read_fail=%lu,samples=%lu,write_ok=%lu,write_fail=%lu,drop_blk=%lu",
                (unsigned long)g_imu_read_ok_count,
                (unsigned long)g_imu_read_fail_count,
                (unsigned long)s_imu_sample_count,
                (unsigned long)s_imu_write_ok,
                (unsigned long)s_imu_write_fail,
                (unsigned long)s_imu_block_drop);
            if (n > 0 && n < (int)sizeof(imu_stats)) {
                SD_DebugLog_WriteLine(imu_stats);
            }
        }
        #endif
        g_ecg_rec.file_seq++;
    }
}

