#include "sd_debug_log.h"
#include "fatfs.h"
#include "ff.h"
#include "cmsis_os.h"
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

static char s_sd_debug_log_path[32] = SD_DEBUG_LOG_BOOT_PATH;
static uint8_t s_sd_debug_mounted = 0;

static uint32_t SD_DebugLog_CurrentSessionSeq(void)
{
    if ((g_ecg_rec.state == ECG_REC_STOPPED ||
         g_ecg_rec.state == ECG_REC_STOPPING) &&
        g_ecg_rec.file_seq > 1U) {
        return g_ecg_rec.file_seq - 1U;
    }

    return g_ecg_rec.file_seq;
}

static FRESULT SD_DebugLog_AppendRaw(const char *text)
{
    FIL file;
    FRESULT res;
    UINT bw = 0;

    if (text == NULL) return FR_INT_ERR;

    if (Mtx_SDCardHandle != NULL) {
        if (osMutexAcquire(Mtx_SDCardHandle, 500) != osOK) {
            Safe_USB_Printf("[SDDBG][ERR] mutex timeout path=%s\r\n", s_sd_debug_log_path);
            return FR_INT_ERR;
        }
    }

    if (!s_sd_debug_mounted) {
        res = f_mount(&SDFatFS, SDPath, 1);
        if (res == FR_OK) {
            s_sd_debug_mounted = 1;
        } else {
            Safe_USB_Printf("[SDDBG][ERR] mount path=%s res=%d\r\n", s_sd_debug_log_path, res);
            goto release;
        }
    }

    /* 宸?mount 鍚庯紝涓嶅湪姣忔潯鏃ュ織閲岄噸澶?f_mount銆?
     * 鏁版嵁 CSV 鎵撳紑鍚庡啀娆?mount 鍚屼竴鍗凤紝鍙兘璁╁凡鎵撳紑鐨?FIL 瀵硅薄澶辨晥銆?*/
    res = f_open(&file, s_sd_debug_log_path, FA_OPEN_APPEND | FA_WRITE);
    if (res == FR_OK) {
        FRESULT wr = f_write(&file, text, strlen(text), &bw);
        if (wr != FR_OK || bw != strlen(text)) {
            Safe_USB_Printf("[SDDBG][ERR] write path=%s res=%d bw=%lu len=%lu\r\n",
                            s_sd_debug_log_path, wr, (unsigned long)bw,
                            (unsigned long)strlen(text));
        }
        f_sync(&file);
        f_close(&file);
    } else {
        Safe_USB_Printf("[SDDBG][ERR] open path=%s res=%d\r\n", s_sd_debug_log_path, res);
    }

release:
    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }

    return res;
}

void SD_DebugLog_Init(void)
{
    char line[128];
    snprintf(s_sd_debug_log_path, sizeof(s_sd_debug_log_path), "%s", SD_DEBUG_LOG_BOOT_PATH);
    snprintf(line, sizeof(line), "\r\nBOOT tick=%lu\r\n", HAL_GetTick());
    SD_DebugLog_AppendRaw(line);
}

void SD_DebugLog_StartNewFile(uint32_t seq)
{
    FIL file;
    FRESULT res;
    UINT bw = 0;
    char line[128];

    snprintf(s_sd_debug_log_path, sizeof(s_sd_debug_log_path),
             "0:/log_%03lu.txt", (unsigned long)seq);

    if (Mtx_SDCardHandle != NULL) {
        if (osMutexAcquire(Mtx_SDCardHandle, 1000) != osOK) {
            Safe_USB_Printf("[SDDBG][ERR] newfile mutex timeout path=%s\r\n", s_sd_debug_log_path);
            return;
        }
    }

    res = f_mount(&SDFatFS, SDPath, 1);
    if (res == FR_OK) {
        s_sd_debug_mounted = 1;
        res = f_open(&file, s_sd_debug_log_path, FA_CREATE_ALWAYS | FA_WRITE);
        if (res == FR_OK) {
            int n = snprintf(line, sizeof(line),
                             "tick,event\r\n%lu,LOG_FILE_BEGIN,seq=%lu\r\n%lu,DIAG_CASE,%d\r\n",
                             (unsigned long)HAL_GetTick(),
                             (unsigned long)seq,
                             (unsigned long)HAL_GetTick(),
                             (int)RECORD_DIAG_CASE);
            if (n > 0 && n < (int)sizeof(line)) {
                f_write(&file, line, (UINT)n, &bw);
            }
            f_sync(&file);
            f_close(&file);
            Safe_USB_Printf("[SDDBG] new log opened path=%s\r\n", s_sd_debug_log_path);
        } else {
            Safe_USB_Printf("[SDDBG][ERR] newfile open path=%s res=%d\r\n", s_sd_debug_log_path, res);
        }
    } else {
        Safe_USB_Printf("[SDDBG][ERR] newfile mount path=%s res=%d\r\n", s_sd_debug_log_path, res);
    }

    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }
}

const char *SD_DebugLog_GetPath(void)
{
    return s_sd_debug_log_path;
}

void SD_DebugLog_WriteLine(const char *line)
{
    char buf[192];
    if (line == NULL) return;
    snprintf(buf, sizeof(buf), "%lu,%s\r\n", HAL_GetTick(), line);
    SD_DebugLog_AppendRaw(buf);
}

void SD_DebugLog_WriteEvent(const char *tag, uint32_t value)
{
    char buf[192];
    if (tag == NULL) return;
    snprintf(buf, sizeof(buf), "%lu,%s,%lu\r\n", HAL_GetTick(), tag, value);
    SD_DebugLog_AppendRaw(buf);
}

void SD_DebugLog_WriteSnapshot(void)
{
    char buf[512];
    snprintf(buf, sizeof(buf),
             "%lu,SNAPSHOT,state=%d,seq=%lu,epoch=%lu,ecg_start=%lu,ecg_stop=%lu,"
             "mic_power=%lu,mic_open=%lu,mic_dma=%lu,mic_first_half=%lu,mic_stop=%lu,"
             "mic_bytes=%lu,mic_halves=%lu,mic_drops=%lu,mic_write_errors=%lu,"
             "samples=%lu,written=%lu,drop=%lu,bytes=%lu,sync=%lu,"
             "fifo=%lu,valid=%lu,fast=%lu,last=%lu,eovf=%lu,etag_ovf=%lu,etag_unknown=%lu,"
             "pll_seen=%lu,pll_edge=%lu,status=0x%06lX\r\n",
             HAL_GetTick(),
             g_ecg_rec.state,
             SD_DebugLog_CurrentSessionSeq(),
             g_ecg_rec.sync_epoch_tick,
             g_ecg_rec.ecg_stream_start_tick,
             g_ecg_rec.ecg_stream_stop_tick,
             g_ecg_rec.mic_power_tick,
             g_ecg_rec.mic_file_open_tick,
             g_ecg_rec.mic_dma_start_tick,
             g_ecg_rec.mic_first_half_tick,
             g_ecg_rec.mic_stop_tick,
             g_ecg_rec.mic_bytes,
             g_ecg_rec.mic_halves,
             g_ecg_rec.mic_drops,
             g_ecg_rec.mic_write_errors,
             g_ecg_rec.ecg_sample_count,
             g_ecg_rec.ecg_written_count,
             g_ecg_rec.ecg_drop_count,
             g_ecg_rec.sd_write_bytes,
             g_ecg_rec.sd_sync_count,
             g_ecg_rec.fifo_sample_count,
             g_ecg_rec.fifo_valid_count,
             g_ecg_rec.fifo_fast_count,
             g_ecg_rec.fifo_last_count,
             g_ecg_rec.fifo_eovf_count,
             g_ecg_rec.fifo_etag_overflow_count,
             g_ecg_rec.fifo_unknown_etag_count,
             g_ecg_rec.pll_status_seen_count,
             g_ecg_rec.pll_edge_count,
             g_ecg_rec.last_status);
    SD_DebugLog_AppendRaw(buf);
}

void SD_DebugLog_WriteSessionSummary(void)
{
    MS_Stats_t stats;
    FIL file;
    FRESULT res;
    UINT bw = 0;
    char path[32];
    char line[768];
    uint32_t seq = SD_DebugLog_CurrentSessionSeq();
    uint32_t duration_ms = 0;
    uint32_t mic_ms = 0;

    MultiSensorLogger_GetStats(&stats);

    if (g_ecg_rec.ecg_stream_stop_tick > g_ecg_rec.ecg_stream_start_tick) {
        duration_ms = g_ecg_rec.ecg_stream_stop_tick - g_ecg_rec.ecg_stream_start_tick;
    }
    mic_ms = AudioRecorder_GetDurationMs();

    snprintf(line, sizeof(line),
             "SESSION_FINAL,seq=%lu,ecg_file=ecg_%03lu.csv,mic_file=mic_%03lu.wav,log_file=log_%03lu.txt,"
             "duration_ms=%lu,ecg_samples=%lu,ecg_write_ok=%lu,ecg_write_fail=%lu,ecg_drop_blk=%lu,"
             "%s=%lu,%s=%lu,%s=%lu,%s=%lu,"
             "imu_samples=%lu,imu_write_ok=%lu,imu_write_fail=%lu,imu_drop_blk=%lu,"
             "mic_bytes=%lu,mic_ms=%lu,mic_halves=%lu,mic_drops=%lu,mic_write_errors=%lu,"
             "sd_bytes=%lu,sd_sync=%lu,writer_blocks=%lu,diag_case=%d,diag_max_gap_ms=%lu,diag_eint=%lu,diag_sd_mtx_max=%lu,diag_audio_wr_max=%lu",
             (unsigned long)seq,
             (unsigned long)seq,
             (unsigned long)seq,
             (unsigned long)seq,
             (unsigned long)duration_ms,
             (unsigned long)stats.ecg_samples,
             (unsigned long)stats.ecg_write_ok,
             (unsigned long)stats.ecg_write_fail,
             (unsigned long)stats.ecg_block_drop,
             SESSION_SUMMARY_PPG_SAMPLES_KEY,
             (unsigned long)stats.ppg_samples,
             SESSION_SUMMARY_PPG_WRITE_OK_KEY,
             (unsigned long)stats.ppg_write_ok,
             SESSION_SUMMARY_PPG_WRITE_FAIL_KEY,
             (unsigned long)stats.ppg_write_fail,
             SESSION_SUMMARY_PPG_DROP_BLK_KEY,
             (unsigned long)stats.ppg_block_drop,
             (unsigned long)stats.imu_samples,
             (unsigned long)stats.imu_write_ok,
             (unsigned long)stats.imu_write_fail,
             (unsigned long)stats.imu_block_drop,
             (unsigned long)g_ecg_rec.mic_bytes,
             (unsigned long)mic_ms,
             (unsigned long)g_ecg_rec.mic_halves,
             (unsigned long)g_ecg_rec.mic_drops,
             (unsigned long)g_ecg_rec.mic_write_errors,
             (unsigned long)stats.sd_write_bytes,
             (unsigned long)stats.sd_sync_count,
             (unsigned long)stats.writer_get_count,
             (int)RECORD_DIAG_CASE,
             (unsigned long)g_ecg_rec.diag_max_gap_ms,
             (unsigned long)g_ecg_rec.diag_eint_hits,
             (unsigned long)g_ecg_rec.diag_sd_mutex_hold_max_ms,
             (unsigned long)g_ecg_rec.diag_audio_write_max_ms);
    SD_DebugLog_WriteLine(line);

    snprintf(path, sizeof(path), "0:/session_%03lu.txt", (unsigned long)seq);

    if (Mtx_SDCardHandle != NULL) {
        if (osMutexAcquire(Mtx_SDCardHandle, 1000) != osOK) {
            Safe_USB_Printf("[SDDBG][ERR] session mutex timeout path=%s\r\n", path);
            return;
        }
    }

    res = f_mount(&SDFatFS, SDPath, 1);
    if (res == FR_OK) {
        res = f_open(&file, path, FA_CREATE_ALWAYS | FA_WRITE);
        if (res == FR_OK) {
            int n = snprintf(line, sizeof(line),
                             "seq=%lu\r\n"
                             "ecg_file=0:/ecg_%03lu.csv\r\n"
                             "mic_file=0:/mic_%03lu.wav\r\n"
                             "log_file=0:/log_%03lu.txt\r\n"
                             "duration_ms=%lu\r\n"
                             "ecg_samples=%lu\r\n"
                             "ecg_write_ok=%lu\r\n"
                             "ecg_write_fail=%lu\r\n"
                             "ecg_block_drop=%lu\r\n"
                             "%s=%lu\r\n"
                             "%s=%lu\r\n"
                             "%s=%lu\r\n"
                             "%s=%lu\r\n"
                             "imu_samples=%lu\r\n"
                             "imu_write_ok=%lu\r\n"
                             "imu_write_fail=%lu\r\n"
                             "imu_block_drop=%lu\r\n"
                             "mic_bytes=%lu\r\n"
                             "mic_ms=%lu\r\n"
                             "mic_halves=%lu\r\n"
                             "mic_drops=%lu\r\n"
                             "mic_write_errors=%lu\r\n"
                             "sd_write_bytes=%lu\r\n"
                             "sd_sync_count=%lu\r\n"
                             "writer_blocks=%lu,diag_case=%d,diag_max_gap_ms=%lu,diag_eint=%lu,diag_sd_mtx_max=%lu,diag_audio_wr_max=%lu\r\n",
                             (unsigned long)seq,
                             (unsigned long)seq,
                             (unsigned long)seq,
                             (unsigned long)seq,
                             (unsigned long)duration_ms,
                             (unsigned long)stats.ecg_samples,
                             (unsigned long)stats.ecg_write_ok,
                             (unsigned long)stats.ecg_write_fail,
                             (unsigned long)stats.ecg_block_drop,
                             SESSION_SUMMARY_PPG_SAMPLES_KEY,
                             (unsigned long)stats.ppg_samples,
                             SESSION_SUMMARY_PPG_WRITE_OK_KEY,
                             (unsigned long)stats.ppg_write_ok,
                             SESSION_SUMMARY_PPG_WRITE_FAIL_KEY,
                             (unsigned long)stats.ppg_write_fail,
                             SESSION_SUMMARY_PPG_DROP_BLK_KEY,
                             (unsigned long)stats.ppg_block_drop,
                             (unsigned long)stats.imu_samples,
                             (unsigned long)stats.imu_write_ok,
                             (unsigned long)stats.imu_write_fail,
                             (unsigned long)stats.imu_block_drop,
                             (unsigned long)g_ecg_rec.mic_bytes,
                             (unsigned long)mic_ms,
                             (unsigned long)g_ecg_rec.mic_halves,
                             (unsigned long)g_ecg_rec.mic_drops,
                             (unsigned long)g_ecg_rec.mic_write_errors,
                             (unsigned long)stats.sd_write_bytes,
                             (unsigned long)stats.sd_sync_count,
                             (unsigned long)stats.writer_get_count,
             (int)RECORD_DIAG_CASE,
             (unsigned long)g_ecg_rec.diag_max_gap_ms,
             (unsigned long)g_ecg_rec.diag_eint_hits,
             (unsigned long)g_ecg_rec.diag_sd_mutex_hold_max_ms,
             (unsigned long)g_ecg_rec.diag_audio_write_max_ms);
            if (n > 0 && n < (int)sizeof(line)) {
                res = f_write(&file, line, (UINT)n, &bw);
                (void)bw;
            }
            f_sync(&file);
            f_close(&file);
        }
    }

    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }

    if (res == FR_OK) {
        Safe_USB_Printf("[SESSION] summary saved path=%s\r\n", path);
    } else {
        Safe_USB_Printf("[SESSION][ERR] summary path=%s res=%d\r\n", path, res);
    }
}








