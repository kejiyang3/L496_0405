#include "session_manager.h"
#include "ecg_record_control.h"
#include "fatfs.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>

extern osMutexId_t Mtx_SDCardHandle;
extern uint8_t RETARGET_RecordFeatureFlags_H_was_included;
extern volatile uint32_t g_ppg_task_call_count;
extern volatile uint32_t g_imu_task_call_count;
extern volatile uint32_t g_ppg_i2c_mutex_wait_ms_total;
extern volatile uint32_t g_ppg_i2c_mutex_wait_ms_max;
extern volatile uint32_t g_ppg_i2c_mutex_hold_ms_total;
extern volatile uint32_t g_ppg_i2c_mutex_hold_ms_max;
extern volatile uint32_t g_imu_i2c_mutex_wait_ms_total;
extern volatile uint32_t g_imu_i2c_mutex_wait_ms_max;
extern volatile uint32_t g_imu_i2c_mutex_hold_ms_total;
extern volatile uint32_t g_imu_i2c_mutex_hold_ms_max;
extern volatile uint32_t g_i2c3_rate_iso_error_count;
extern volatile uint8_t g_ppg_ie1;
extern volatile uint8_t g_ppg_fifo_cfg;
extern volatile uint8_t g_ppg_mode;
extern volatile uint8_t g_ppg_spo2_cfg;
extern volatile uint8_t g_icm_who;
extern volatile uint8_t g_icm_pwr1;
extern volatile uint8_t g_icm_int_cfg;
extern volatile uint8_t g_icm_int_en1;
extern volatile uint8_t g_icm_accel_cfg;
extern volatile uint16_t g_icm_accel_div;
extern volatile uint8_t g_icm_gyro_cfg;
extern volatile uint8_t g_icm_gyro_div;
extern volatile uint8_t g_icm_odr_align;
extern volatile uint8_t g_icm_user_ctrl;
extern volatile uint8_t g_icm_lp_config;
extern volatile uint8_t g_icm_pwr2;
extern volatile uint8_t g_icm_fifo_en2;
extern volatile uint8_t g_icm_fifo_mode;
extern volatile uint16_t g_icm_fifo_count;
#define RECORD_FEATURE_FLAGS_DEFINED
#include "record_feature_flags.h"
#include "multi_sensor_logger.h"
#include "audio_recorder.h"

SessionInfo_t g_session = {0};

void Session_Reset(void)
{
    memset(&g_session, 0, sizeof(g_session));
}

int Session_Create(uint32_t tick_now)
{
    if (g_session.created) return 0;

    snprintf(g_session.session_id, sizeof(g_session.session_id),
             "%04u%02u%02u_%02u%02u%02u_T%08lu",
             SESSION_DEFAULT_YEAR, SESSION_DEFAULT_MONTH, SESSION_DEFAULT_DAY,
             SESSION_DEFAULT_HOUR, SESSION_DEFAULT_MIN, SESSION_DEFAULT_SEC,
             (unsigned long)tick_now);

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

    char path[80];
    snprintf(path, sizeof(path), "%s/session.txt", g_session.session_dir);

    FIL fp; FRESULT res;
    if (Mtx_SDCardHandle != NULL) osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
    res = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (res == FR_OK) {
        const char *mic_status =
            ((unsigned)RECORD_EXPERIMENT_ENABLE_WAV && g_audio_recorder_diag.file_open_ok == 0U) ? "MIC_NO_WAV" :
            ((unsigned)RECORD_EXPERIMENT_ENABLE_WAV && g_audio_recorder_diag.wav_header_finalized == 0U) ? "MIC_WAV_HEADER_FAIL" :
            "MIC_OK";
        char buf[3072];
        int len = snprintf(buf, sizeof(buf),
            "experiment_mode=%s\r\n"
            "session_id=%s\r\n"
            "session_dir=%s\r\n"
            "requested_duration_ms=%lu\r\n"
            "actual_duration_ms=%lu\r\n"
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
            "audio_enabled=%u\r\n"
            "wav_enabled=%u\r\n"
            "core_enabled=%u\r\n"
            "mic_enabled=%u\r\n"
            "mic_mode=%s\r\n"
            "mic_status=%s\r\n"
            "mic_wav_expected=%u\r\n"
            "mic_wav_path=%s\r\n"
            "mic_file_open_attempted=%lu\r\n"
            "mic_file_open_ok=%lu\r\n"
            "mic_file_open_result=%ld\r\n"
            "mic_file_close_attempted=%lu\r\n"
            "mic_file_close_ok=%lu\r\n"
            "mic_file_close_result=%ld\r\n"
            "mic_file_bytes=%lu\r\n"
            "mic_wav_data_bytes=%lu\r\n"
            "mic_wav_sample_rate=%lu\r\n"
            "mic_wav_header_finalized=%lu\r\n"
            "mic_last_fresult=%ld\r\n"
            "mic_last_error=%lu\r\n"
            "mic_audio_task_started=%lu\r\n"
            "mic_sai_dma_started=%lu\r\n"
            "mic_dma_half_count=%lu\r\n"
            "mic_dma_full_count=%lu\r\n"
            "mic_last_dma_tick=%lu\r\n"
            "mic_last_write_tick=%lu\r\n"
            "mic_blocks_in=%lu\r\n"
            "mic_blocks_written=%lu\r\n"
            "mic_blocks_dropped=%lu\r\n"
            "mic_blocks_write_failed=%lu\r\n"
            "mic_drop_count=%lu\r\n"
            "mic_stall_count=%lu\r\n"
            "mic_effective_duration=%lu\r\n"
            "mic_power_pin=GPIOB12\r\n"
            "mic_power_active_level=HIGH\r\n"
            "mic_power_on_count=%lu\r\n"
            "mic_power_off_count=%lu\r\n"
            "mic_power_on_tick=%lu\r\n"
            "mic_power_off_tick=%lu\r\n"
            "mic_power_state_at_start=%lu\r\n"
            "mic_power_state_at_stop=%lu\r\n",
            RECORD_EXPERIMENT_LABEL,
            g_session.session_id,
            g_session.session_dir,
            (unsigned long)RECORD_DEFAULT_RECORD_MS,
            (unsigned long)g_session.duration_ms,
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
            (unsigned)RECORD_ENABLE_AUDIO,
            (unsigned)RECORD_EXPERIMENT_ENABLE_WAV,
            (unsigned)RECORD_EXPERIMENT_ENABLE_CORE,
            (unsigned)RECORD_EXPERIMENT_ENABLE_MIC,
            (unsigned)RECORD_EXPERIMENT_ENABLE_WAV ? "WAV" :
                ((unsigned)RECORD_EXPERIMENT_ENABLE_MIC_DMA ? "DMA_ONLY" : "OFF"),
            mic_status,
            (unsigned)RECORD_EXPERIMENT_ENABLE_WAV,
            g_audio_recorder_diag.wav_path,
            (unsigned long)g_audio_recorder_diag.file_open_attempted,
            (unsigned long)g_audio_recorder_diag.file_open_ok,
            (long)g_audio_recorder_diag.file_open_result,
            (unsigned long)g_audio_recorder_diag.file_close_attempted,
            (unsigned long)g_audio_recorder_diag.file_close_ok,
            (long)g_audio_recorder_diag.file_close_result,
            (unsigned long)g_audio_recorder_diag.file_bytes,
            (unsigned long)g_audio_recorder_diag.wav_data_bytes,
            (unsigned long)g_audio_recorder_diag.wav_sample_rate,
            (unsigned long)g_audio_recorder_diag.wav_header_finalized,
            (long)g_audio_recorder_diag.last_fresult,
            (unsigned long)g_audio_recorder_diag.last_error,
            (unsigned long)g_audio_recorder_diag.audio_task_started,
            (unsigned long)g_audio_recorder_diag.sai_dma_started,
            (unsigned long)g_audio_recorder_diag.dma_half_count,
            (unsigned long)g_audio_recorder_diag.dma_full_count,
            (unsigned long)g_audio_recorder_diag.last_dma_tick,
            (unsigned long)g_audio_recorder_diag.last_write_tick,
            (unsigned long)g_audio_recorder_diag.blocks_in,
            (unsigned long)g_audio_recorder_diag.blocks_written,
            (unsigned long)g_audio_recorder_diag.blocks_dropped,
            (unsigned long)g_audio_recorder_diag.blocks_write_failed,
            (unsigned long)g_ecg_rec.mic_drops,
            (unsigned long)g_audio_recorder_diag.stall_count,
            (unsigned long)AudioRecorder_GetDurationMs(),
            (unsigned long)g_audio_recorder_diag.power_on_count,
            (unsigned long)g_audio_recorder_diag.power_off_count,
            (unsigned long)g_audio_recorder_diag.power_on_tick,
            (unsigned long)g_audio_recorder_diag.power_off_tick,
            (unsigned long)g_audio_recorder_diag.power_state_at_start,
            (unsigned long)g_audio_recorder_diag.power_state_at_stop);
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

    char path[80];
    snprintf(path, sizeof(path), "%s/diag_summary.txt", g_session.session_dir);

    FIL fp; FRESULT res;
    if (Mtx_SDCardHandle != NULL) osMutexAcquire(Mtx_SDCardHandle, osWaitForever);
    res = f_open(&fp, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (res == FR_OK) {
        UINT bw;
        char line[640];
#define WRITE_DIAG_LINE(...) do { \
            int n__ = snprintf(line, sizeof(line), __VA_ARGS__); \
            if (n__ > 0 && n__ < (int)sizeof(line)) { \
                f_write(&fp, line, (UINT)n__, &bw); \
            } \
        } while (0)
        WRITE_DIAG_LINE("ecg_eovf_count=%lu\r\n", (unsigned long)g_ecg_rec.fifo_eovf_count);
        WRITE_DIAG_LINE("ecg_fifo_empty_count=%lu\r\n", (unsigned long)g_ecg_rec.fifo_empty_count);
        WRITE_DIAG_LINE("ecg_unknown_etag=%lu\r\n", (unsigned long)g_ecg_rec.fifo_unknown_etag_count);
        WRITE_DIAG_LINE("ecg_pack_drop=%lu\r\n", (unsigned long)g_ecg_rec.pack_add_drop_count);
        WRITE_DIAG_LINE("ecg_queue_fail=%lu\r\n", (unsigned long)g_ecg_rec.ecg_queue_submit_fail);
        WRITE_DIAG_LINE("ecg_writer_blocks=%lu\r\n", (unsigned long)g_ecg_rec.ecg_writer_get_blocks);
        WRITE_DIAG_LINE("mic_drop_blocks=%lu\r\n", (unsigned long)g_ecg_rec.mic_drops);
        WRITE_DIAG_LINE("sd_write_errors=%lu\r\n", (unsigned long)g_ecg_rec.ecg_sd_write_error_count);
        WRITE_DIAG_LINE("max_gap_recording_ms=%lu\r\n", (unsigned long)g_ecg_rec.diag_max_gap_recording_ms);
        WRITE_DIAG_LINE("max_gap_task_ms=%lu\r\n", (unsigned long)g_ecg_rec.diag_max_gap_ms);
        WRITE_DIAG_LINE("pll_status_seen=%lu\r\n", (unsigned long)g_ecg_rec.pll_status_seen_count);
        WRITE_DIAG_LINE("pll_edges=%lu\r\n", (unsigned long)g_ecg_rec.pll_edge_count);
        WRITE_DIAG_LINE("task_calls=%lu\r\n", (unsigned long)g_ecg_rec.diag_task_calls);
        WRITE_DIAG_LINE("notify_wakes=%lu\r\n", (unsigned long)g_ecg_rec.diag_notify_wakes);
        WRITE_DIAG_LINE("notify_timeouts=%lu\r\n", (unsigned long)g_ecg_rec.diag_notify_timeouts);
        WRITE_DIAG_LINE("experiment_mode=%s\r\n", RECORD_EXPERIMENT_LABEL);
        WRITE_DIAG_LINE("session_id=%s\r\n", g_session.session_id);
        WRITE_DIAG_LINE("session_dir=%s\r\n", g_session.session_dir);
        WRITE_DIAG_LINE("requested_duration_ms=%lu\r\n", (unsigned long)RECORD_DEFAULT_RECORD_MS);
        WRITE_DIAG_LINE("actual_duration_ms=%lu\r\n", (unsigned long)g_session.duration_ms);
        WRITE_DIAG_LINE("audio_enabled=%u\r\n", (unsigned)RECORD_ENABLE_AUDIO);
        WRITE_DIAG_LINE("wav_enabled=%u\r\n", (unsigned)RECORD_EXPERIMENT_ENABLE_WAV);
        WRITE_DIAG_LINE("core_enabled=%u\r\n", (unsigned)RECORD_EXPERIMENT_ENABLE_CORE);
        WRITE_DIAG_LINE("MIC_STATUS=%s\r\n",
                        ((unsigned)RECORD_EXPERIMENT_ENABLE_WAV && g_audio_recorder_diag.file_open_ok == 0U) ? "MIC_NO_WAV" :
                        ((unsigned)RECORD_EXPERIMENT_ENABLE_WAV && g_audio_recorder_diag.wav_header_finalized == 0U) ? "MIC_WAV_HEADER_FAIL" :
                        "MIC_OK");
        WRITE_DIAG_LINE("MIC_WAV,mic_wav_path=%s,mic_file_open_attempted=%lu,mic_file_open_ok=%lu,mic_file_open_result=%ld,mic_file_bytes=%lu,mic_wav_data_bytes=%lu,mic_blocks_in=%lu,mic_blocks_written=%lu,mic_blocks_dropped=%lu,mic_last_error=%lu,mic_dma_half_count=%lu,mic_dma_full_count=%lu\r\n",
                        g_audio_recorder_diag.wav_path,
                        (unsigned long)g_audio_recorder_diag.file_open_attempted,
                        (unsigned long)g_audio_recorder_diag.file_open_ok,
                        (long)g_audio_recorder_diag.file_open_result,
                        (unsigned long)g_audio_recorder_diag.file_bytes,
                        (unsigned long)g_audio_recorder_diag.wav_data_bytes,
                        (unsigned long)g_audio_recorder_diag.blocks_in,
                        (unsigned long)g_audio_recorder_diag.blocks_written,
                        (unsigned long)g_audio_recorder_diag.blocks_dropped,
                        (unsigned long)g_audio_recorder_diag.last_error,
                        (unsigned long)g_audio_recorder_diag.dma_half_count,
                        (unsigned long)g_audio_recorder_diag.dma_full_count);
        WRITE_DIAG_LINE("RATE_ISO_CONFIG,case=%u,label=%s,ecg=%u,ppg=%u,icm=%u,audio=%u,no_sd=%u,ppg_avg1=%u,icm_batch=%u\r\n",
                        (unsigned)RECORD_RATE_ISO_CASE, RECORD_RATE_ISO_LABEL,
                        (unsigned)RECORD_RATE_ISO_ENABLE_ECG, (unsigned)RECORD_ENABLE_PPG,
                        (unsigned)RECORD_ENABLE_ICM, (unsigned)RECORD_ENABLE_AUDIO,
                        (unsigned)RECORD_RATE_ISO_NO_SD, (unsigned)RECORD_RATE_ISO_PPG_AVG1,
                        (unsigned)RECORD_RATE_ISO_ICM_BATCH);
        WRITE_DIAG_LINE("RATE_ISO_PPG_REG,ppg_ie1=0x%02X,ppg_fifo_cfg=0x%02X,ppg_mode=0x%02X,ppg_spo2=0x%02X\r\n",
                        (unsigned)g_ppg_ie1, (unsigned)g_ppg_fifo_cfg,
                        (unsigned)g_ppg_mode, (unsigned)g_ppg_spo2_cfg);
        WRITE_DIAG_LINE("RATE_ISO_ICM_REG,icm_who=0x%02X,icm_user_ctrl=0x%02X,icm_lp_config=0x%02X,icm_pwr1=0x%02X,icm_pwr2=0x%02X,icm_int_cfg=0x%02X,icm_int_en1=0x%02X,icm_fifo_en2=0x%02X,icm_fifo_mode=0x%02X,icm_fifo_count=%u,icm_accel_cfg=0x%02X,icm_accel_div=%u,icm_gyro_cfg=0x%02X,icm_gyro_div=%u,icm_odr_align=0x%02X\r\n",
                        (unsigned)g_icm_who, (unsigned)g_icm_user_ctrl,
                        (unsigned)g_icm_lp_config, (unsigned)g_icm_pwr1,
                        (unsigned)g_icm_pwr2,
                        (unsigned)g_icm_int_cfg, (unsigned)g_icm_int_en1,
                        (unsigned)g_icm_fifo_en2, (unsigned)g_icm_fifo_mode,
                        (unsigned)g_icm_fifo_count,
                        (unsigned)g_icm_accel_cfg, (unsigned)g_icm_accel_div,
                        (unsigned)g_icm_gyro_cfg, (unsigned)g_icm_gyro_div,
                        (unsigned)g_icm_odr_align);
        WRITE_DIAG_LINE("RATE_ISO_I2C,ppg_task_calls=%lu,imu_task_calls=%lu,ppg_mutex_wait_ms=%lu,ppg_mutex_wait_max=%lu,ppg_mutex_hold_ms=%lu,ppg_mutex_hold_max=%lu,imu_mutex_wait_ms=%lu,imu_mutex_wait_max=%lu,imu_mutex_hold_ms=%lu,imu_mutex_hold_max=%lu,i2c_errors=%lu\r\n",
                        (unsigned long)g_ppg_task_call_count,
                        (unsigned long)g_imu_task_call_count,
                        (unsigned long)g_ppg_i2c_mutex_wait_ms_total,
                        (unsigned long)g_ppg_i2c_mutex_wait_ms_max,
                        (unsigned long)g_ppg_i2c_mutex_hold_ms_total,
                        (unsigned long)g_ppg_i2c_mutex_hold_ms_max,
                        (unsigned long)g_imu_i2c_mutex_wait_ms_total,
                        (unsigned long)g_imu_i2c_mutex_wait_ms_max,
                        (unsigned long)g_imu_i2c_mutex_hold_ms_total,
                        (unsigned long)g_imu_i2c_mutex_hold_ms_max,
                        (unsigned long)g_i2c3_rate_iso_error_count);
#undef WRITE_DIAG_LINE
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

    char path[80];
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
            "ECG,1,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%.1f,%lu,%lu,OK\r\n",
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
            "PPG,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%.1f,%lu,%lu,OK\r\n",
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
            "IMU,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%.1f,%lu,%lu,OK\r\n",
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
            "MIC,%u,%u,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%lu,%.1f,%lu,%lu,OK\r\n",
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
