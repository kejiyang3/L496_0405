#ifndef __AUDIO_RECORDER_H__
#define __AUDIO_RECORDER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct {
    char wav_path[64];
    volatile uint32_t audio_task_started;
    volatile uint32_t file_open_attempted;
    volatile uint32_t file_open_ok;
    volatile int32_t file_open_result;
    volatile uint32_t file_close_attempted;
    volatile uint32_t file_close_ok;
    volatile int32_t file_close_result;
    volatile uint32_t file_bytes;
    volatile uint32_t wav_data_bytes;
    volatile uint32_t wav_sample_rate;
    volatile uint32_t wav_header_finalized;
    volatile int32_t last_fresult;
    volatile uint32_t last_error;
    volatile uint32_t sai_dma_started;
    volatile uint32_t dma_half_count;
    volatile uint32_t dma_full_count;
    volatile uint32_t last_dma_tick;
    volatile uint32_t last_write_tick;
    volatile uint32_t blocks_in;
    volatile uint32_t blocks_written;
    volatile uint32_t blocks_dropped;
    volatile uint32_t blocks_write_failed;
    volatile uint32_t bytes_captured;
    volatile uint32_t bytes_written;
    volatile uint32_t stall_count;
    volatile uint32_t power_on_count;
    volatile uint32_t power_off_count;
    volatile uint32_t power_on_tick;
    volatile uint32_t power_off_tick;
    volatile uint32_t power_state_at_start;
    volatile uint32_t power_state_at_stop;
} AudioRecorderDiag_t;

extern AudioRecorderDiag_t g_audio_recorder_diag;

void AudioRecorder_Task(void *argument);
uint8_t AudioRecorder_IsActive(void);
uint32_t AudioRecorder_GetEffectiveSampleRateHz(void);
uint32_t AudioRecorder_GetDurationMs(void);
uint32_t AudioRecorder_GetSetupEnterCount(void);
uint32_t AudioRecorder_GetSkipNoSdFile(void);
uint32_t AudioRecorder_GetSkipSameSeq(void);
uint32_t AudioRecorder_GetLoopCount(void);
uint32_t AudioRecorder_GetLoopDelta(void);
uint32_t AudioRecorder_GetMutexTimeouts(void);
uint32_t AudioRecorder_GetDroppedBlocks(void);
uint32_t AudioRecorder_GetMaxMutexWaitMs(void);
uint32_t AudioRecorder_GetMaxMutexHoldMs(void);

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_RECORDER_H__ */

