#include "session_manager.h"
#include "ecg_record_control.h"
#include "fatfs.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

extern osMutexId_t Mtx_SDCardHandle;
extern uint8_t RETARGET_RecordFeatureFlags_H_was_included;
#define RECORD_FEATURE_FLAGS_DEFINED
#include "record_feature_flags.h"
#include "multi_sensor_logger.h"

SessionInfo_t g_session = {0};

int Session_Create(uint32_t tick_now)
{
    if (g_session.created) return 0;

    snprintf(g_session.session_id, sizeof(g_session.session_id),
             "%04u%02u%02u_%02u%02u%02u",
             SESSION_DEFAULT_YEAR, SESSION_DEFAULT_MONTH, SESSION_DEFAULT_DAY,
             SESSION_DEFAULT_HOUR, SESSION_DEFAULT_MIN, SESSION_DEFAULT_SEC);

    snprintf(g_session.session_dir, sizeof(g_session.session_dir),
             "%s/%s", SESSION_DIR_PREFIX, g_session.session_id);

    snprintf(g_session.dt_start, sizeof(g_session.dt_start),
             "%04u-%02u-%02uT%02u:%02u:%02u",
             SESSION_DEFAULT_YEAR, SESSION_DEFAULT_MONTH, SESSION_DEFAULT_DAY,
             SESSION_DEFAULT_HOUR, SESSION_DEFAULT_MIN, SESSION_DEFAULT_SEC);

    g_session.tick_start_ms = tick_now;
    g_session.tick_end_ms = 0;
    g_session.duration_ms = 0;
    memset(g_session.dt_end, 0, sizeof(g_session.dt_end));

    FRESULT res = f_mkdir(SESSION_DIR_PREFIX);
    if (res != FR_OK && res != FR_EXIST) return -1;

    res = f_mkdir(g_session.session_dir);
    if (res != FR_OK && res != FR_EXIST) return -2;

    g_session.created = 1;
    return 0;
}

int Session_WriteMeta(void)
{
    if (!g_session.created) return -1;

    uint32_t es = g_session.duration_ms / 1000U;
    uint32_t eh = SESSION_DEFAULT_HOUR + (es / 3600U);
    uint32_t em = SESSION_DEFAULT_MIN  + ((es % 3600U) / 60U);
    uint32_t ess = SESSION_DEFAULT_SEC + (es % 60U);
    while (ess >= 60) { ess -= 60; em++; }
    while (em  >= 60) { em  -= 60; eh++; }

    snprintf(g_session.dt_end, sizeof(g_session.dt_end),
             "%04u-%02u-%02uT%02u:%02u:%02u",
             SESSION_DEFAULT_YEAR, SESSION_DEFAULT_MONTH, SESSION_DEFAULT_DAY,
             (unsigned)eh, (unsigned)em, (unsigned)ess);

    char path[64];
    snprintf(path, sizeof(path), "%s/session.txt", g_session.session_dir);

    FIL fp; FRESULT res;
    if (Mtx_SDCardHandle != NULL) osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
    res = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (res == FR_OK) {
        char buf[512];
        int len = snprintf(buf, sizeof(buf),
            "session_id=%s\r\n"
            "device_time_start=%s\r\n"
            "device_time_end=%s\r\n"
            "start_tick_ms=%lu\r\n"
            "end_tick_ms=%lu\r\n"
            "duration_ms=%lu\r\n"
            "time_source=device_datetime_plus_tick_ms\r\n"
            "configured_ecg_sps=%u\r\n"
            "actual_ecg_samples=%lu\r\n"
            "actual_ecg_avg_sps=%.1f\r\n"
            "ecg_eovf=%lu\r\n"
            "ecg_fifo_empty=%lu\r\n"
            "audio_configured_sps=%u\r\n"
            "audio_actual_bytes=%lu\r\n"
            "ppg_enabled=%u\r\n"
            "imu_enabled=%u\r\n"
            "audio_enabled=%u\r\n",
            g_session.session_id,
            g_session.dt_start, g_session.dt_end,
            (unsigned long)g_session.tick_start_ms,
            (unsigned long)g_session.tick_end_ms,
            (unsigned long)g_session.duration_ms,
            (unsigned)RECORD_ECG_SAMPLE_RATE_HZ,
            (unsigned long)g_ecg_rec.ecg_sample_count,
            g_session.duration_ms > 0
                ? (float)g_ecg_rec.ecg_sample_count * 1000.0f / (float)g_session.duration_ms
                : 0.0f,
            (unsigned long)g_ecg_rec.fifo_eovf_count,
            (unsigned long)g_ecg_rec.fifo_empty_count,
            (unsigned)RECORD_MIC_SAMPLE_RATE_HZ,
            (unsigned long)g_ecg_rec.mic_bytes,
            (unsigned)RECORD_ENABLE_PPG,
            (unsigned)RECORD_ENABLE_ICM,
            (unsigned)RECORD_ENABLE_AUDIO);
        UINT bw;
        f_write(&fp, buf, (UINT)len, &bw);
        f_close(&fp);
    }
    if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
    return (res == FR_OK) ? 0 : -3;
}

int Session_WriteDiagSummary(void)
{
    if (!g_session.created) return -1;

    char path[64];
    snprintf(path, sizeof(path), "%s/diag_summary.txt", g_session.session_dir);

    FIL fp; FRESULT res;
    if (Mtx_SDCardHandle != NULL) osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
    res = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (res == FR_OK) {
        char buf[512];
        int len = snprintf(buf, sizeof(buf),
            "ecg_eovf_count=%lu\r\n"
            "ecg_fifo_empty_count=%lu\r\n"
            "ecg_unknown_etag=%lu\r\n"
            "ecg_pack_drop=%lu\r\n"
            "ecg_queue_fail=%lu\r\n"
            "ecg_writer_blocks=%lu\r\n"
            "mic_drop_blocks=%lu\r\n"
            "sd_write_errors=%lu\r\n"
            "max_gap_recording_ms=%lu\r\n"
            "max_gap_task_ms=%lu\r\n"
            "pll_status_seen=%lu\r\n"
            "pll_edges=%lu\r\n"
            "task_calls=%lu\r\n"
            "notify_wakes=%lu\r\n"
            "notify_timeouts=%lu\r\n",
            (unsigned long)g_ecg_rec.fifo_eovf_count,
            (unsigned long)g_ecg_rec.fifo_empty_count,
            (unsigned long)g_ecg_rec.fifo_unknown_etag_count,
            (unsigned long)g_ecg_rec.pack_add_drop_count,
            (unsigned long)g_ecg_rec.ecg_queue_submit_fail,
            (unsigned long)g_ecg_rec.ecg_writer_get_blocks,
            (unsigned long)g_ecg_rec.mic_drops,
            (unsigned long)g_ecg_rec.ecg_sd_write_error_count,
            (unsigned long)g_ecg_rec.diag_max_gap_recording_ms,
            (unsigned long)g_ecg_rec.diag_max_gap_ms,
            (unsigned long)g_ecg_rec.pll_status_seen_count,
            (unsigned long)g_ecg_rec.pll_edge_count,
            (unsigned long)g_ecg_rec.diag_task_calls,
            (unsigned long)g_ecg_rec.diag_notify_wakes,
            (unsigned long)g_ecg_rec.diag_notify_timeouts);
        UINT bw;
        f_write(&fp, buf, (UINT)len, &bw);
        f_close(&fp);
    }
    if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
    return (res == FR_OK) ? 0 : -3;
}

int Session_WriteModalitySummary(void)
{
    if (!g_session.created) return -1;

    MS_Stats_t stats;
    MultiSensorLogger_GetStats(&stats);

    char path[64];
    snprintf(path, sizeof(path), "%s/modality_summary.csv", g_session.session_dir);

    FIL fp; FRESULT res;
    if (Mtx_SDCardHandle != NULL) osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
    res = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (res == FR_OK) {
        const char *hdr = "modality,enabled,configured_sps,capture_count,queue_submit_ok,queue_submit_fail,writer_get_count,written_count,file_count,first_tick_ms,last_tick_ms,duration_ms,actual_avg_sps,drop_count,error_count,status\r\n";
        UINT bw;
        f_write(&fp, hdr, (UINT)strlen(hdr), &bw);

        char line[256];
        uint32_t dur = g_session.duration_ms;
        if (dur == 0) dur = 1;
        int n;
        float sps;

        /* ECG */
        sps = (float)g_ecg_rec.ecg_sample_count * 1000.0f / (float)dur;
        n = snprintf(line, sizeof(line),
            "ECG,1,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%.1f,%lu,%lu,OK\r\n",
            (unsigned)RECORD_ECG_SAMPLE_RATE_HZ,
            (unsigned long)g_ecg_rec.ecg_sample_count,
            (unsigned long)stats.ecg_submit_ok,
            (unsigned long)stats.ecg_submit_fail,
            (unsigned long)stats.writer_get_count,
            (unsigned long)stats.ecg_write_ok,
            (unsigned long)stats.ecg_write_ok,
            (unsigned long)g_session.tick_start_ms,
            (unsigned long)g_session.tick_end_ms,
            (unsigned long)dur, (double)sps,
            (unsigned long)stats.ecg_block_drop,
            (unsigned long)g_ecg_rec.fifo_eovf_count);
        if (n > 0) f_write(&fp, line, (UINT)n, &bw);

        /* PPG */
        sps = (float)stats.ppg_samples * 1000.0f / (float)dur;
        n = snprintf(line, sizeof(line),
            "PPG,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%.1f,%lu,%lu,OK\r\n",
            (unsigned)RECORD_ENABLE_PPG,
            (unsigned)RECORD_PPG_SAMPLE_RATE_HZ,
            (unsigned long)stats.ppg_samples,
            (unsigned long)0UL, (unsigned long)0UL, (unsigned long)0UL,
            (unsigned long)stats.ppg_write_ok,
            (unsigned long)stats.ppg_write_ok,
            (unsigned long)g_session.tick_start_ms,
            (unsigned long)g_session.tick_end_ms,
            (unsigned long)dur, (double)sps,
            (unsigned long)stats.ppg_block_drop,
            (unsigned long)0UL);
        if (n > 0) f_write(&fp, line, (UINT)n, &bw);

        /* IMU */
        sps = (float)stats.imu_samples * 1000.0f / (float)dur;
        n = snprintf(line, sizeof(line),
            "IMU,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%.1f,%lu,%lu,OK\r\n",
            (unsigned)RECORD_ENABLE_ICM,
            (unsigned)104,
            (unsigned long)stats.imu_samples,
            (unsigned long)0UL, (unsigned long)0UL, (unsigned long)0UL,
            (unsigned long)stats.imu_write_ok,
            (unsigned long)stats.imu_write_ok,
            (unsigned long)g_session.tick_start_ms,
            (unsigned long)g_session.tick_end_ms,
            (unsigned long)dur, (double)sps,
            (unsigned long)stats.imu_block_drop,
            (unsigned long)0UL);
        if (n > 0) f_write(&fp, line, (UINT)n, &bw);

        /* MIC */
        uint32_t mic_samples = g_ecg_rec.mic_bytes / 2U;
        sps = (float)mic_samples * 1000.0f / (float)dur;
        n = snprintf(line, sizeof(line),
            "MIC,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%.1f,%lu,%lu,OK\r\n",
            (unsigned)RECORD_ENABLE_AUDIO,
            (unsigned)RECORD_MIC_SAMPLE_RATE_HZ,
            (unsigned long)mic_samples,
            (unsigned long)0UL, (unsigned long)0UL, (unsigned long)0UL,
            (unsigned long)mic_samples,
            (unsigned long)mic_samples,
            (unsigned long)g_session.tick_start_ms,
            (unsigned long)g_session.tick_end_ms,
            (unsigned long)dur, (double)sps,
            (unsigned long)g_ecg_rec.mic_drops,
            (unsigned long)g_ecg_rec.mic_write_errors);
        if (n > 0) f_write(&fp, line, (UINT)n, &bw);

        f_close(&fp);
    }
    if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
    return (res == FR_OK) ? 0 : -3;
}
