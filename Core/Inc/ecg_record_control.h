#ifndef __ECG_RECORD_CONTROL_H__
#define __ECG_RECORD_CONTROL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    volatile uint32_t acquire_count;
    volatile uint32_t acquire_timeout;
    volatile uint32_t wait_ms_last;
    volatile uint32_t wait_ms_max;
    volatile uint32_t hold_ms_last;
    volatile uint32_t hold_ms_max;
    volatile uint32_t write_ms_last;
    volatile uint32_t write_ms_max;
    volatile uint32_t sync_ms_last;
    volatile uint32_t sync_ms_max;
    volatile uint32_t bytes_written;
    volatile uint32_t write_error;
} SD_PathDiag_t;

typedef enum {
    ECG_REC_IDLE = 0,
    ECG_REC_RECORDING,
    ECG_REC_STOPPING,
    ECG_REC_STOPPED,
    ECG_REC_ERROR
} ECG_RecordState_t;

typedef struct {
    volatile ECG_RecordState_t state;
    volatile uint8_t request_start;
    volatile uint8_t request_stop;
    volatile uint8_t request_usb_info;

    volatile uint32_t start_tick;
    volatile uint32_t stop_tick;
    volatile uint32_t sync_epoch_tick;
    volatile uint32_t ecg_stream_start_tick;
    volatile uint32_t ecg_stream_stop_tick;
    volatile uint32_t mic_power_tick;
    volatile uint32_t mic_file_open_tick;
    volatile uint32_t mic_dma_start_tick;
    volatile uint32_t mic_first_half_tick;
    volatile uint32_t mic_stop_tick;
    volatile uint32_t mic_bytes;
    volatile uint32_t mic_halves;
    volatile uint32_t mic_drops;
    volatile uint32_t mic_write_errors;

    volatile uint32_t ecg_sample_count;
    volatile uint32_t ecg_written_count;
    volatile uint32_t ecg_drop_count;
    volatile uint32_t sd_write_bytes;
    volatile uint32_t sd_sync_count;
    volatile uint32_t fifo_eovf_count;
    volatile uint32_t pll_warn_count;

    /* STATUS / PLL / FIFO diagnostics */
    volatile uint32_t last_status;
    volatile uint32_t pll_status_seen_count;
    volatile uint32_t pll_edge_count;
    volatile uint8_t  pll_current_set;
    volatile uint32_t fifo_sample_count;
    volatile uint32_t fifo_valid_count;
    volatile uint32_t fifo_fast_count;
    volatile uint32_t fifo_last_count;
    volatile uint32_t fifo_empty_count;
    volatile uint32_t fifo_etag_overflow_count;
    volatile uint32_t fifo_unknown_etag_count;

    /* Deep diagnostics for ECG rate investigation */
    volatile uint32_t diag_task_calls;
    volatile uint32_t diag_eint_hits;
    volatile uint32_t diag_max_gap_ms;
    volatile uint32_t diag_last_call_tick;
    volatile uint32_t diag_irq_snapshot;
    volatile uint32_t diag_notify_timeouts;
    volatile uint32_t diag_notify_wakes;

    /* ETAG histogram */
    volatile uint32_t etag_hist[8];

    /* SPI statistics */
    volatile uint32_t max30003_spi_read_count;
    volatile uint32_t max30003_spi_write_count;
    volatile uint32_t max30003_spi_burst_count;
    volatile uint32_t max30003_spi_error_count;
    volatile uint32_t max30003_spi_timeout_count;
    volatile uint32_t max30003_burst_read_error_count;
    volatile uint32_t max30003_burst_us_last;
    volatile uint32_t max30003_burst_us_max;
    volatile uint32_t max30003_burst_us_sum;
    volatile uint32_t max30003_burst_us_count;

    /* Packagedata layer counters */
    volatile uint32_t pack_add_attempt_count;
    volatile uint32_t pack_add_ok_count;
    volatile uint32_t pack_add_drop_count;
    volatile uint32_t pack_buffer_level_max;

    /* SD write layer counters */
    volatile uint32_t ecg_sd_write_sample_count;
    volatile uint32_t ecg_sd_write_block_count;
    volatile uint32_t ecg_sd_write_error_count;
    volatile uint32_t ecg_sd_flush_count;

    /* === Diagnostic: SD mutex + audio write timing === */
    volatile uint32_t diag_sd_mutex_hold_max_ms;
    volatile uint32_t diag_audio_write_max_ms;
    volatile uint32_t diag_sd_mutex_contention_count;

    /* === INTB/EXTI raw diagnostics === */
    volatile uint32_t diag_exti_raw_count;      /* EXTI9_5 ISR invocations */
    volatile uint32_t diag_intb_low_count;      /* PB6 GPIO read as low */
    volatile uint32_t diag_intb_high_count;     /* PB6 GPIO read as high */
    volatile uint32_t diag_status_eint_total;   /* total STATUS.EINT seen */
    volatile uint32_t diag_status_eovf_total;   /* total STATUS.EOVF seen */

    volatile uint8_t sd_file_opened;
    volatile uint8_t sd_file_closed;

    volatile uint8_t request_save_info;
    volatile uint32_t auto_stop_ms;
    volatile uint32_t requested_record_ms;

    volatile uint32_t file_seq;
    char file_name[32];
} ECG_RecordControl_t;

extern ECG_RecordControl_t g_ecg_rec;

extern volatile uint32_t g_max30003_cnfg_ecg_readback;
extern volatile uint32_t g_max30003_cnfg_gen_readback;

void ECG_RequestStart(void);
void ECG_RequestStop(void);
void ECG_RequestUsbInfo(void);
void ECG_RequestSaveInfo(void);
void ECG_UpdateFileName(void);
void ECG_ResetStats(void);

#ifdef __cplusplus
}
#endif

extern SD_PathDiag_t g_sd_audio_diag;
extern SD_PathDiag_t g_sd_csv_diag;
extern SD_PathDiag_t g_sd_debug_diag;

#endif /* __ECG_RECORD_CONTROL_H__ */
