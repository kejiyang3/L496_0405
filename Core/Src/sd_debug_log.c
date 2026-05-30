#include "sd_debug_log.h"
#include "fatfs.h"
#include "ff.h"
#include "cmsis_os.h"
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "ecg_record_control.h"
#include "multi_sensor_logger.h"
#include "audio_recorder.h"
#include "session_summary_fields.h"
#include <stdio.h>
#include <string.h>

extern osMutexId_t Mtx_SDCardHandle;
extern void Safe_USB_Printf(const char *format, ...);

#define SD_DEBUG_LOG_BOOT_PATH "0:/debug_log.txt"

/* === RAM Ring Buffer === */
#define DEBUG_RING_SLOTS    32
#define DEBUG_RING_SLOT_SZ  192
#define DEBUG_FLUSH_MS      1000    /* flush every 1s if data pending */
#define DEBUG_FLUSH_BATCH   8       /* flush up to 8 lines per batch */

typedef struct {
    char data[DEBUG_RING_SLOT_SZ];
    uint16_t len;
} DebugRingEntry_t;

static DebugRingEntry_t s_ring[DEBUG_RING_SLOTS];
static volatile uint32_t s_ring_write = 0;  /* producer index */
static volatile uint32_t s_ring_read  = 0;  /* consumer index */
static volatile uint32_t s_ring_drops = 0;  /* dropped lines */
static volatile uint8_t  s_ring_overflowed = 0;

static char s_sd_debug_log_path[32] = SD_DEBUG_LOG_BOOT_PATH;
static uint8_t s_sd_debug_mounted = 0;
static TaskHandle_t s_writer_task_handle = NULL;
static volatile uint8_t s_stop_flush_requested = 0;

static uint32_t SD_DebugLog_CurrentSessionSeq(void)
{
    if ((g_ecg_rec.state == ECG_REC_STOPPED ||
         g_ecg_rec.state == ECG_REC_STOPPING) &&
        g_ecg_rec.file_seq > 1U) {
        return g_ecg_rec.file_seq - 1U;
    }
    return g_ecg_rec.file_seq;
}

/* --- Ring buffer producer (called from any task) --- */
static uint8_t ring_push(const char *text, uint16_t len)
{
    uint32_t next = (s_ring_write + 1) % DEBUG_RING_SLOTS;
    if (next == s_ring_read) {
        /* buffer full ? drop oldest */
        s_ring_read = (s_ring_read + 1) % DEBUG_RING_SLOTS;
        s_ring_drops++;
        s_ring_overflowed = 1;
    }
    uint16_t copy_len = (len < DEBUG_RING_SLOT_SZ - 1) ? len : (DEBUG_RING_SLOT_SZ - 1);
    memcpy(s_ring[s_ring_write].data, text, copy_len);
    s_ring[s_ring_write].data[copy_len] = '\0';
    s_ring[s_ring_write].len = copy_len;
    s_ring_write = next;
    return 1;
}

/* --- Ring buffer consumer (called from writer task) --- */
static uint8_t ring_pop(char *out, uint16_t max_len)
{
    if (s_ring_read == s_ring_write) return 0;
    uint16_t copy_len = s_ring[s_ring_read].len;
    if (copy_len > max_len - 1) copy_len = max_len - 1;
    memcpy(out, s_ring[s_ring_read].data, copy_len);
    out[copy_len] = '\0';
    s_ring_read = (s_ring_read + 1) % DEBUG_RING_SLOTS;
    return 1;
}

static uint32_t ring_count(void)
{
    return (s_ring_write >= s_ring_read)
        ? (s_ring_write - s_ring_read)
        : (DEBUG_RING_SLOTS - s_ring_read + s_ring_write);
}

/* --- Internal: actually write one line to SD (called from writer task only) --- */
static FRESULT debug_flush_one_line(FIL *fp, const char *text)
{
    UINT bw = 0;
    FRESULT res;

    if (!s_sd_debug_mounted) {
        res = f_mount(&SDFatFS, SDPath, 1);
        if (res == FR_OK) {
            s_sd_debug_mounted = 1;
        } else {
            return res;
        }
    }

    res = f_write(fp, text, strlen(text), &bw);
    if (res == FR_OK && bw == strlen(text)) {
        g_sd_debug_diag.bytes_written += bw;
    } else {
        g_sd_debug_diag.write_error++;
    }
    return res;
}

/* --- Non-blocking public API --- */

void SD_DebugLog_Init(void)
{
    memset(s_ring, 0, sizeof(s_ring));
    s_ring_write = 0;
    s_ring_read  = 0;
    s_ring_drops = 0;
    s_ring_overflowed = 0;
    s_stop_flush_requested = 0;
    /* queue a BOOT line */
    char line[128];
    snprintf(line, sizeof(line), "%lu,BOOT\r\n", HAL_GetTick());
    ring_push(line, (uint16_t)strlen(line));
}

void SD_DebugLog_StartNewFile(uint32_t seq)
{
    char line[128];
    int n = snprintf(line, sizeof(line),
                     "%lu,LOG_FILE_BEGIN,seq=%lu\r\n%lu,DIAG_CASE,%d\r\n",
                     (unsigned long)HAL_GetTick(),
                     (unsigned long)seq,
                     (unsigned long)HAL_GetTick(),
                     (int)RECORD_DIAG_CASE);
    if (n > 0 && n < (int)sizeof(line)) {
        ring_push(line, (uint16_t)n);
    }
    /* update log file path */
    snprintf(s_sd_debug_log_path, sizeof(s_sd_debug_log_path),
             "0:/log_%03lu.txt", (unsigned long)seq);
    /* s_sd_debug_mounted = 0; -- removed: f_mount would invalidate CSV file handles */
}

const char *SD_DebugLog_GetPath(void)
{
    return s_sd_debug_log_path;
}

void SD_DebugLog_WriteLine(const char *line)
{
    if (line == NULL) return;
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%lu,%s\r\n", HAL_GetTick(), line);
    if (n > 0 && n < (int)sizeof(buf)) {
        ring_push(buf, (uint16_t)n);
    }
}

void SD_DebugLog_WriteEvent(const char *tag, uint32_t value)
{
    if (tag == NULL) return;
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "%lu,%s,%lu\r\n", HAL_GetTick(), tag, (unsigned long)value);
    if (n > 0 && n < (int)sizeof(buf)) {
        ring_push(buf, (uint16_t)n);
    }
}

void SD_DebugLog_WriteSnapshot(void)
{
    char buf[640];
    int n = snprintf(buf, sizeof(buf),
        "%lu,SNAPSHOT,state=%d,seq=%lu,epoch=%lu,ecg_start=%lu,ecg_stop=%lu,"
        "mic_power=%lu,mic_open=%lu,mic_dma=%lu,mic_first_half=%lu,mic_stop=%lu,"
        "mic_bytes=%lu,mic_halves=%lu,mic_drops=%lu,mic_write_errors=%lu,"
        "samples=%lu,written=%lu,drop=%lu,bytes=%lu,sync=%lu,"
        "fifo=%lu,valid=%lu,fast=%lu,last=%lu,eovf=%lu,etag_ovf=%lu,etag_unknown=%lu,"
        "pll_seen=%lu,pll_edge=%lu,status=0x%06lX\r\n",
        HAL_GetTick(),
        g_ecg_rec.state,
        (unsigned long)SD_DebugLog_CurrentSessionSeq(),
        (unsigned long)g_ecg_rec.sync_epoch_tick,
        (unsigned long)g_ecg_rec.ecg_stream_start_tick,
        (unsigned long)g_ecg_rec.ecg_stream_stop_tick,
        (unsigned long)g_ecg_rec.mic_power_tick,
        (unsigned long)g_ecg_rec.mic_file_open_tick,
        (unsigned long)g_ecg_rec.mic_dma_start_tick,
        (unsigned long)g_ecg_rec.mic_first_half_tick,
        (unsigned long)g_ecg_rec.mic_stop_tick,
        (unsigned long)g_ecg_rec.mic_bytes,
        (unsigned long)g_ecg_rec.mic_halves,
        (unsigned long)g_ecg_rec.mic_drops,
        (unsigned long)g_ecg_rec.mic_write_errors,
        (unsigned long)g_ecg_rec.ecg_sample_count,
        (unsigned long)g_ecg_rec.ecg_written_count,
        (unsigned long)g_ecg_rec.ecg_drop_count,
        (unsigned long)g_ecg_rec.sd_write_bytes,
        (unsigned long)g_ecg_rec.sd_sync_count,
        (unsigned long)g_ecg_rec.fifo_sample_count,
        (unsigned long)g_ecg_rec.fifo_valid_count,
        (unsigned long)g_ecg_rec.fifo_fast_count,
        (unsigned long)g_ecg_rec.fifo_last_count,
        (unsigned long)g_ecg_rec.fifo_eovf_count,
        (unsigned long)g_ecg_rec.fifo_etag_overflow_count,
        (unsigned long)g_ecg_rec.fifo_unknown_etag_count,
        (unsigned long)g_ecg_rec.pll_status_seen_count,
        (unsigned long)g_ecg_rec.pll_edge_count,
        (unsigned long)g_ecg_rec.last_status);
    if (n > 0 && n < (int)sizeof(buf)) {
        ring_push(buf, (uint16_t)n);
    }

    /* SDSTAT diag */
    {
        char sdstat[256];
        n = snprintf(sdstat, sizeof(sdstat),
            "SDSTAT,"
            "audio_wait_max=%lu,audio_hold_max=%lu,audio_write_max=%lu,audio_sync_max=%lu,audio_timeout=%lu,audio_bytes=%lu,"
            "csv_wait_max=%lu,csv_hold_max=%lu,csv_write_max=%lu,csv_sync_max=%lu,csv_timeout=%lu,"
            "debug_wait_max=%lu,debug_hold_max=%lu,debug_write_max=%lu,debug_sync_max=%lu,debug_timeout=%lu",
            (unsigned long)g_sd_audio_diag.wait_ms_max,
            (unsigned long)g_sd_audio_diag.hold_ms_max,
            (unsigned long)g_sd_audio_diag.write_ms_max,
            (unsigned long)g_sd_audio_diag.sync_ms_max,
            (unsigned long)g_sd_audio_diag.acquire_timeout,
            (unsigned long)g_sd_audio_diag.bytes_written,
            (unsigned long)g_sd_csv_diag.wait_ms_max,
            (unsigned long)g_sd_csv_diag.hold_ms_max,
            (unsigned long)g_sd_csv_diag.write_ms_max,
            (unsigned long)g_sd_csv_diag.sync_ms_max,
            (unsigned long)g_sd_csv_diag.acquire_timeout,
            (unsigned long)g_sd_debug_diag.wait_ms_max,
            (unsigned long)g_sd_debug_diag.hold_ms_max,
            (unsigned long)g_sd_debug_diag.write_ms_max,
            (unsigned long)g_sd_debug_diag.sync_ms_max,
            (unsigned long)g_sd_debug_diag.acquire_timeout);
        if (n > 0 && n < (int)sizeof(sdstat)) {
            ring_push(sdstat, (uint16_t)n);
        }
    }
}

void SD_DebugLog_WriteSessionSummary(void)
{
    MS_Stats_t stats;
    MultiSensorLogger_GetStats(&stats);

    uint32_t seq = SD_DebugLog_CurrentSessionSeq();
    uint32_t duration_ms = 0;
    uint32_t mic_ms = 0;
    if (g_ecg_rec.ecg_stream_stop_tick > g_ecg_rec.ecg_stream_start_tick) {
        duration_ms = g_ecg_rec.ecg_stream_stop_tick - g_ecg_rec.ecg_stream_start_tick;
    }
    mic_ms = AudioRecorder_GetDurationMs();

    char buf[800];
    int n = snprintf(buf, sizeof(buf),
        "SESSION_FINAL,seq=%lu,ecg_file=ecg_%03lu.csv,mic_file=mic_%03lu.wav,log_file=log_%03lu.txt,"
        "duration_ms=%lu,ecg_samples=%lu,ecg_write_ok=%lu,ecg_write_fail=%lu,ecg_drop_blk=%lu,"
        "%s=%lu,%s=%lu,%s=%lu,%s=%lu,"
        "imu_samples=%lu,imu_write_ok=%lu,imu_write_fail=%lu,imu_drop_blk=%lu,"
        "mic_bytes=%lu,mic_ms=%lu,mic_halves=%lu,mic_drops=%lu,mic_write_errors=%lu,"
        "sd_bytes=%lu,sd_sync=%lu,writer_blocks=%lu,diag_case=%d,diag_max_gap_ms=%lu,diag_eint=%lu,diag_sd_mtx_max=%lu,diag_audio_wr_max=%lu",
        (unsigned long)seq, (unsigned long)seq, (unsigned long)seq, (unsigned long)seq,
        (unsigned long)duration_ms,
        (unsigned long)stats.ecg_samples, (unsigned long)stats.ecg_write_ok,
        (unsigned long)stats.ecg_write_fail, (unsigned long)stats.ecg_block_drop,
        SESSION_SUMMARY_PPG_SAMPLES_KEY, (unsigned long)stats.ppg_samples,
        SESSION_SUMMARY_PPG_WRITE_OK_KEY, (unsigned long)stats.ppg_write_ok,
        SESSION_SUMMARY_PPG_WRITE_FAIL_KEY, (unsigned long)stats.ppg_write_fail,
        SESSION_SUMMARY_PPG_DROP_BLK_KEY, (unsigned long)stats.ppg_block_drop,
        (unsigned long)stats.imu_samples, (unsigned long)stats.imu_write_ok,
        (unsigned long)stats.imu_write_fail, (unsigned long)stats.imu_block_drop,
        (unsigned long)g_ecg_rec.mic_bytes, (unsigned long)mic_ms,
        (unsigned long)g_ecg_rec.mic_halves, (unsigned long)g_ecg_rec.mic_drops,
        (unsigned long)g_ecg_rec.mic_write_errors,
        (unsigned long)stats.sd_write_bytes, (unsigned long)stats.sd_sync_count,
        (unsigned long)stats.writer_get_count,
        (int)RECORD_DIAG_CASE,
        (unsigned long)g_ecg_rec.diag_max_gap_ms,
        (unsigned long)g_ecg_rec.diag_eint_hits,
        (unsigned long)g_ecg_rec.diag_sd_mutex_hold_max_ms,
        (unsigned long)g_ecg_rec.diag_audio_write_max_ms);
    if (n > 0 && n < (int)sizeof(buf)) {
        ring_push(buf, (uint16_t)n);
    }
}

/* --- Flush control --- */
void SD_DebugLog_RequestStopFlush(void)
{
    s_stop_flush_requested = 1;
}

uint8_t SD_DebugLog_IsFlushComplete(void)
{
    return (ring_count() == 0) ? 1 : 0;
}

/* ================================================================
 * DebugLogWriter Task ? low priority, batch SD writes
 * ================================================================ */
void DebugLogWriter_Task(void *argument)
{
    (void)argument;
    s_writer_task_handle = xTaskGetCurrentTaskHandle();
    FIL file;
    uint8_t file_open = 0;
    uint32_t last_flush_tick = 0;

    /* small delay for system init */
    osDelay(100);

    for (;;) {
        uint32_t count = ring_count();

        if (count == 0U) {
            if (s_stop_flush_requested) {
                /* all flushed, done */
                s_stop_flush_requested = 0;
            }
            osDelay(50);
            continue;
        }

        uint32_t now = HAL_GetTick();
        uint8_t should_flush = 0;

        /* flush when: buffer > 50% full OR 1s elapsed OR stop requested */
        if (count > DEBUG_RING_SLOTS / 2) {
            should_flush = 1;
        } else if (now - last_flush_tick >= DEBUG_FLUSH_MS) {
            should_flush = 1;
        } else if (s_stop_flush_requested) {
            should_flush = 1;
        }

        if (!should_flush) {
            osDelay(20);
            continue;
        }

        /* --- Batch flush to SD --- */
        if (Mtx_SDCardHandle != NULL) {
            if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(10)) != osOK) {
                /* mutex busy ? retry later, don't block ECG */
                osDelay(50);
                continue;
            }
        }

        /* open file if needed */
        if (!file_open) {
            FRESULT res = f_open(&file, s_sd_debug_log_path,
                                 FA_OPEN_APPEND | FA_WRITE | FA_OPEN_ALWAYS);
            if (res == FR_OK) {
                file_open = 1;
            } else {
                if (Mtx_SDCardHandle != NULL) osMutexRelease(Mtx_SDCardHandle);
                osDelay(100);
                continue;
            }
        }

        /* flush up to DEBUG_FLUSH_BATCH lines */
        uint32_t flushed = 0;
        char line[DEBUG_RING_SLOT_SZ + 4];
        while (flushed < DEBUG_FLUSH_BATCH && ring_count() > 0) {
            if (ring_pop(line, sizeof(line))) {
                debug_flush_one_line(&file, line);
                flushed++;
            }
        }

        if (flushed > 0) {
            f_sync(&file);
            f_close(&file);
            file_open = 0;
            last_flush_tick = HAL_GetTick();
        }

        if (Mtx_SDCardHandle != NULL) {
            osMutexRelease(Mtx_SDCardHandle);
        }

        /* if stop requested and ring is empty, close file */
        if (s_stop_flush_requested && ring_count() == 0 && file_open) {
            if (Mtx_SDCardHandle != NULL) {
                osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(50));
            }
            f_sync(&file);
            f_close(&file);
            file_open = 0;
            s_stop_flush_requested = 0;
            if (Mtx_SDCardHandle != NULL) {
                osMutexRelease(Mtx_SDCardHandle);
            }
        }
    }
}
