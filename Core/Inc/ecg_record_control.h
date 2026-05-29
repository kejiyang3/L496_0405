#ifndef __ECG_RECORD_CONTROL_H__
#define __ECG_RECORD_CONTROL_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

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

    /* 新增: STATUS / PLL / FIFO 详细诊断 */
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
    /* === Deep diagnostics for ECG rate investigation === */
    volatile uint32_t diag_task_calls;         /* total calls to MAX30003_Task */
    volatile uint32_t diag_eint_hits;          /* calls where EINT was set */
    volatile uint32_t diag_max_gap_ms;         /* max ms between consecutive MAX30003_Task calls */
    volatile uint32_t diag_last_call_tick;     /* tick of last MAX30003_Task call */
    volatile uint32_t diag_irq_snapshot;       /* ecg_irq_count at snapshot time */
    volatile uint32_t diag_notify_timeouts;    /* ulTaskNotifyTake timeouts (5ms) */
    volatile uint32_t diag_notify_wakes;       /* ulTaskNotifyTake wake-by-notification */

    /* === ETAG histogram === */
    volatile uint32_t etag_hist[8];

    /* === SPI statistics === */
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

    /* === Packagedata layer counters === */
    volatile uint32_t pack_add_attempt_count;
    volatile uint32_t pack_add_ok_count;
    volatile uint32_t pack_add_drop_count;
    volatile uint32_t pack_buffer_level_max;

    /* === SD write layer counters === */
    volatile uint32_t ecg_sd_write_sample_count;
    volatile uint32_t ecg_sd_write_block_count;
    volatile uint32_t ecg_sd_write_error_count;
    volatile uint32_t ecg_sd_flush_count;

    volatile uint8_t sd_file_opened;
    volatile uint8_t sd_file_closed;

    volatile uint8_t request_save_info;
    volatile uint32_t auto_stop_ms;   /* >0 时录制自动持续该时长(ms)后停�?*/
    volatile uint32_t requested_record_ms; /* 下一�?start 的自动停止时�? 0=手动停止 */

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

#endif /* __ECG_RECORD_CONTROL_H__ */
