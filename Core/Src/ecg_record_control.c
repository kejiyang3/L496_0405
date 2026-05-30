#include "ecg_record_control.h"
#include "session_manager.h"
#include "record_feature_flags.h"
#include "main.h"
#include <stdio.h>
#include <string.h>


SD_PathDiag_t g_sd_audio_diag = {0};
SD_PathDiag_t g_sd_csv_diag   = {0};
SD_PathDiag_t g_sd_debug_diag = {0};

ECG_RecordControl_t g_ecg_rec = {
    .state = ECG_REC_IDLE,
    .request_start = 0,
    .request_stop = 0,
    .request_usb_info = 0,
    .request_save_info = 0,
    .sync_epoch_tick = 0,
    .ecg_stream_start_tick = 0,
    .ecg_stream_stop_tick = 0,
    .mic_power_tick = 0,
    .mic_file_open_tick = 0,
    .mic_dma_start_tick = 0,
    .mic_first_half_tick = 0,
    .mic_stop_tick = 0,
    .mic_bytes = 0,
    .mic_halves = 0,
    .mic_drops = 0,
    .mic_write_errors = 0,
    .sd_file_opened = 0,
    .sd_file_closed = 1,
    .file_seq = 10,  /* Skip old files on full SD */
    .fifo_eovf_count = 0,
    .pll_warn_count = 0,
    .last_status = 0,
    .pll_status_seen_count = 0,
    .pll_edge_count = 0,
    .pll_current_set = 0,
    .fifo_sample_count = 0,
    .fifo_empty_count = 0,
    .fifo_etag_overflow_count = 0,
    .auto_stop_ms = 0,
    .requested_record_ms = 0,
    .file_name = "0:/ecg_001.csv"
};

/* 鏍规嵁褰撳墠 file_seq 鏇存�?file_name */
void ECG_UpdateFileName(void)
{
    snprintf(g_ecg_rec.file_name, sizeof(g_ecg_rec.file_name),
             "0:/ecg_%03lu.csv", g_ecg_rec.file_seq);
}

void ECG_RequestStart(void)
{
    ECG_UpdateFileName();
    g_ecg_rec.requested_record_ms = RECORD_DEFAULT_RECORD_MS;
    g_ecg_rec.request_start = 1;
}

void ECG_RequestStop(void)
{
    g_ecg_rec.request_stop = 1;
}

void ECG_RequestUsbInfo(void)
{
    g_ecg_rec.request_usb_info = 1;
}

void ECG_RequestSaveInfo(void)
{
    g_ecg_rec.request_save_info = 1;
}

/* 姣忔寮€濮嬫柊璁板綍鍓嶉噸缃墍鏈夌粺�?*/
void ECG_ResetStats(void)
{
    g_ecg_rec.last_status = 0;

    g_ecg_rec.pll_warn_count = 0;
    g_ecg_rec.pll_status_seen_count = 0;
    g_ecg_rec.pll_edge_count = 0;
    g_ecg_rec.pll_current_set = 0;

    g_ecg_rec.fifo_sample_count = 0;
    g_ecg_rec.fifo_valid_count = 0;
    g_ecg_rec.fifo_fast_count = 0;
    g_ecg_rec.fifo_last_count = 0;
    g_ecg_rec.fifo_eovf_count = 0;
    g_ecg_rec.fifo_empty_count = 0;
    g_ecg_rec.fifo_etag_overflow_count = 0;
    g_ecg_rec.fifo_unknown_etag_count = 0;
}
