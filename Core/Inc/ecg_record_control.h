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

    volatile uint8_t sd_file_opened;
    volatile uint8_t sd_file_closed;

    volatile uint8_t request_save_info;
    volatile uint32_t auto_stop_ms;   /* >0 时录制自动持续该时长(ms)后停止 */
    volatile uint32_t requested_record_ms; /* 下一次 start 的自动停止时长, 0=手动停止 */

    volatile uint32_t file_seq;
    char file_name[32];
} ECG_RecordControl_t;

extern ECG_RecordControl_t g_ecg_rec;

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
