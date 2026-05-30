/*
 * audio_run_diag.h - AUDIO_RUN 每秒诊断计数器
 *
 * 用途: 追踪 MIC 通道在每个录制周期中的健康状态。
 *       所有计数器在 StartTask_Audio 进入新 session 时清零。
 */

#ifndef AUDIO_RUN_DIAG_H
#define AUDIO_RUN_DIAG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    /* 任务存活 */
    volatile uint32_t audio_task_alive;      /* 任务循环计数 */
    volatile uint32_t audio_state;           /* 0=idle 1=init 2=recording 3=stopping 4=error */

    /* SAI DMA */
    volatile uint32_t sai_dma_started;       /* SAI DMA 启动次数 */
    volatile uint32_t dma_half_count;        /* DMA 半满中断计数 */
    volatile uint32_t dma_full_count;        /* DMA 全满中断计数 */
    volatile uint32_t dma_error_count;       /* DMA 错误计数 */

    /* 数据流 */
    volatile uint32_t blocks_in;             /* 从 DMA 接收的 half-block 数 */
    volatile uint32_t blocks_written;        /* 成功写入 SD 的 half-block 数 */
    volatile uint32_t blocks_dropped;        /* 写入失败/丢弃的 half-block 数 */
    volatile uint32_t bytes_captured;        /* DMA 捕获的总字节数 */
    volatile uint32_t bytes_written;         /* SD 写入的总字节数 */

    /* 时间戳 */
    volatile uint32_t last_dma_tick;         /* 最后一次 DMA 中断的 tick */
    volatile uint32_t last_write_tick;       /* 最后一次 SD 写入的 tick */
    volatile uint32_t last_diag_tick;        /* 最后一次诊断输出的 tick */

    /* 错误 */
    volatile uint32_t last_error_code;       /* 最后一次 WRITE/SYNC 返回的 FRESULT */
    volatile uint32_t mutex_timeouts;        /* SD 互斥锁超时次数 */

    /* WAV */
    volatile uint32_t wav_file_open;         /* WAV 文件是否打开 (0/1) */
    volatile uint32_t wav_data_bytes;        /* WAV data chunk 的当前字节数 */

} AudioRunDiag_t;

extern AudioRunDiag_t g_audio_run_diag;

void AudioRunDiag_Reset(void);
void AudioRunDiag_LogSnapshot(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_RUN_DIAG_H */