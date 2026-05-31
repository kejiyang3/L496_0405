/*
 * audio_run_diag.h - AUDIO_RUN 每秒诊断计数器 + AUDIO_STALL 寄存器快照
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

/* ===== AUDIO_RUN 每秒诊断 ===== */

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

/* ===== AUDIO_STALL 寄存器快照 ===== */

#define AUDIO_STALL_CAPTURE_COUNT 4U  /* 最多捕获 4 次 stall */

typedef struct {
    /* 捕获元数据 */
    volatile uint32_t capture_tick;          /* 捕获时刻的 HAL_GetTick() */
    volatile uint32_t capture_seq;           /* 第几次 stall 捕获 (1-based) */
    volatile uint32_t stall_reason;          /* 0=watchdog 1=dma_error 2=restart_fail */
    volatile uint32_t s_audio_halves;        /* 当前 s_audio_halves 值 */

    /* DMA2_Channel6 寄存器 (外设地址 0x40020400 + 0x68) */
    volatile uint32_t dma_ccr;               /* DMA CCR (控制寄存器) */
    volatile uint32_t dma_cndtr;             /* DMA CNDTR (剩余传输数) */
    volatile uint32_t dma_isr_chan;          /* DMA ISR 中 Channel 6 相关位 */

    /* DMAMUX Channel 6 (外设地址 0x40020800 + 0x18) */
    volatile uint32_t dmamux_ccr;            /* DMAMUX CCR (请求线配置) */

    /* SAI1 Block A 寄存器 (外设地址 0x40015400) */
    volatile uint32_t sai_sr;                /* SAI SR (状态寄存器) */
    volatile uint32_t sai_cr1;               /* SAI CR1 (控制寄存器1) */
    volatile uint32_t sai_cr2;               /* SAI CR2 (控制寄存器2) */

    /* HAL 状态 */
    volatile uint32_t hal_sai_state;         /* hsai_BlockA1.State */
    volatile uint32_t hal_sai_error;         /* hsai_BlockA1.ErrorCode */
    volatile uint32_t hal_dma_state;         /* hdma_sai1_a.State */

    /* DMA alive 推断 */
    volatile uint32_t stall_timeout_count;   /* stall 触发时的连续超时计数 */

} AudioStallCapture_t;

/* ===== 全局变量 ===== */

extern AudioRunDiag_t g_audio_run_diag;
extern AudioStallCapture_t g_audio_stall_captures[AUDIO_STALL_CAPTURE_COUNT];
extern volatile uint32_t g_audio_stall_capture_count;

/* ===== 函数 ===== */

void AudioRunDiag_Reset(void);
void AudioRunDiag_LogSnapshot(void);

/* 在 stall 检测点调用，捕获 DMA/SAI/HAL 寄存器现场 */
void AudioStallCapture_Snapshot(uint32_t reason, uint32_t halves, uint32_t timeout_count);

/* 将已捕获的所有 stall 快照写入 SD 调试日志 */
void AudioStallCapture_LogAll(void);

#ifdef __cplusplus
}
#endif

#endif /* AUDIO_RUN_DIAG_H */