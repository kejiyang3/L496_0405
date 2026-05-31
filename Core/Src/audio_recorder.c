#include "audio_recorder.h"

#include "FreeRTOS.h"
#include "cmsis_os.h"
#include "task.h"
#include "ecg_record_control.h"
#include "fatfs.h"
#include "ff.h"
#include "main.h"
#include "sai.h"
#include "audio_sample_format.h"
#include "audio_wav_format.h"
#include "sd_debug_log.h"
#include "record_feature_flags.h"
#include "session_manager.h"
#include "audio_run_diag.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern osMutexId_t Mtx_SDCardHandle;
extern void Safe_USB_Printf(const char *format, ...);
extern DMA_HandleTypeDef hdma_sai1_a;

#define AUDIO_SAMPLE_RATE_HZ RECORD_MIC_SAMPLE_RATE_HZ
#define AUDIO_USB_VERBOSE           0
#define AUDIO_DMA_BYTES             (64U * 1024U)
#define AUDIO_DMA_WORDS             (AUDIO_DMA_BYTES / sizeof(uint32_t))
#define AUDIO_DMA_HALF_WORDS        (AUDIO_DMA_WORDS / 2U)
#define AUDIO_PCM_HALF_SAMPLES      (AUDIO_DMA_HALF_WORDS / 2U)
#define AUDIO_NOTIFY_HALF0          (1UL << 0)
#define AUDIO_NOTIFY_HALF1          (1UL << 1)
#define AUDIO_SYNC_EVERY_HALVES     128U
#define AUDIO_MIN_RATE_HZ           1000U
#define AUDIO_MAX_RATE_HZ           48000U

__attribute__((section(".sram2"), aligned(4)))
static uint32_t s_audio_dma_buf[AUDIO_DMA_WORDS];

static TaskHandle_t s_audio_task_handle = NULL;
static FIL s_audio_file;
static FIL s_audio_blocks_file;
static uint8_t s_audio_blocks_open = 0;
static uint32_t s_audio_blocks_seq = 0;
static uint8_t s_audio_file_open = 0;
static volatile uint8_t s_audio_recording_active = 0;
static uint32_t s_audio_bytes = 0;
static uint32_t s_audio_halves = 0;
static volatile uint32_t s_audio_dma_drops = 0;
static volatile uint32_t s_audio_mutex_timeouts = 0;
static volatile uint32_t s_audio_max_write_ms = 0;
static volatile uint32_t s_audio_half_irq_count = 0;
static int16_t s_audio_min_pcm = 32767;
static int16_t s_audio_max_pcm = -32768;
static volatile uint32_t s_audio_failed_seq = 0;
static int16_t s_pcm_half[AUDIO_PCM_HALF_SAMPLES];

AudioRunDiag_t g_audio_run_diag = {0};
AudioStallCapture_t g_audio_stall_captures[AUDIO_STALL_CAPTURE_COUNT] = {0};
volatile uint32_t g_audio_stall_capture_count = 0;


static void build_wav_header(uint8_t hdr[44], uint32_t data_bytes, uint32_t sample_rate_hz);

static uint32_t audio_missed_half_count(void)
{
    uint32_t irq_count = s_audio_half_irq_count;
    return (irq_count > s_audio_halves) ? (irq_count - s_audio_halves) : 0U;
}

static void audio_update_drop_count(void)
{
    g_ecg_rec.mic_drops = s_audio_dma_drops +
                          s_audio_mutex_timeouts +
                          audio_missed_half_count();
}

uint32_t AudioRecorder_GetDurationMs(void)
{
    uint32_t start = g_ecg_rec.mic_dma_start_tick;
    uint32_t stop = g_ecg_rec.mic_stop_tick;

    if (start == 0U) {
        return 0;
    }
    if (stop > start) {
        return stop - start;
    }
    if (s_audio_recording_active || s_audio_file_open) {
        return HAL_GetTick() - start;
    }
    return 0;
}

uint32_t AudioRecorder_GetEffectiveSampleRateHz(void)
{
    uint32_t duration_ms = AudioRecorder_GetDurationMs();
    return AUDIO_WAV_EFFECTIVE_RATE_HZ(s_audio_bytes,
                                       duration_ms,
                                       AUDIO_SAMPLE_RATE_HZ,
                                       AUDIO_MIN_RATE_HZ,
                                       AUDIO_MAX_RATE_HZ);
}

static FRESULT audio_update_header_and_sync_locked(void)
{
    uint8_t hdr[44];
    FRESULT res;
    UINT bw = 0;

    build_wav_header(hdr, s_audio_bytes, RECORD_MIC_SAMPLE_RATE_HZ);

    res = f_lseek(&s_audio_file, 0);
    if (res == FR_OK) {
        res = f_write(&s_audio_file, hdr, sizeof(hdr), &bw);
    }
    if (res == FR_OK && bw != sizeof(hdr)) {
        res = FR_DISK_ERR;
    }
    if (res == FR_OK) {
        res = f_lseek(&s_audio_file, 44U + s_audio_bytes);
    }
    if (res == FR_OK) {
        res = f_sync(&s_audio_file);
    }

    return res;
}

static void write_le16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffU);
    p[1] = (uint8_t)((v >> 8) & 0xffU);
}

static void write_le32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xffU);
    p[1] = (uint8_t)((v >> 8) & 0xffU);
    p[2] = (uint8_t)((v >> 16) & 0xffU);
    p[3] = (uint8_t)((v >> 24) & 0xffU);
}

static void build_wav_header(uint8_t hdr[44], uint32_t data_bytes, uint32_t sample_rate_hz)
{
    memset(hdr, 0, 44);
    memcpy(&hdr[0], "RIFF", 4);
    write_le32(&hdr[4], 36U + data_bytes);
    memcpy(&hdr[8], "WAVEfmt ", 8);
    write_le32(&hdr[16], 16U);
    write_le16(&hdr[20], 1U);
    write_le16(&hdr[22], 1U);
    write_le32(&hdr[24], sample_rate_hz);
    write_le32(&hdr[28], sample_rate_hz * 2U);
    write_le16(&hdr[32], 2U);
    write_le16(&hdr[34], 16U);
    memcpy(&hdr[36], "data", 4);
    write_le32(&hdr[40], data_bytes);
}

static FRESULT audio_write_locked(const void *data, UINT len)
{
    UINT bw = 0;
    FRESULT res;
    uint32_t t0 = HAL_GetTick();
    uint32_t wait_ms = 0, hold_ms = 0, write_ms = 0;

    g_sd_audio_diag.acquire_count++;

    if (Mtx_SDCardHandle != NULL) {
        uint32_t tw = HAL_GetTick();
#if RECORD_FIX_AUDIO_NONBLOCKING_DISCARD
        /* P3: 5ms non-blocking */
        if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(5)) != osOK) {
#else
        if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(20)) != osOK) {
#endif
            s_audio_mutex_timeouts++;
            g_sd_audio_diag.acquire_timeout++;
            return FR_TIMEOUT;
        }
        wait_ms = HAL_GetTick() - tw;
        g_sd_audio_diag.wait_ms_last = wait_ms;
        if (wait_ms > g_sd_audio_diag.wait_ms_max) g_sd_audio_diag.wait_ms_max = wait_ms;
    }

    uint32_t th = HAL_GetTick();
    if (RECORD_DIAG_AUDIO_MIC_SD_WRITE && !RECORD_DIAG_DISABLE_ALL_SD_WRITES) {
#if RECORD_TEST_AUDIO_DROP_BEFORE_FWRITE
        /* F1: drop before f_write to test if f_write is the necessary trigger */
        res = FR_OK;
        bw = len;
        write_ms = 0;
        g_sd_audio_diag.write_ms_last = 0;
#else
        uint32_t twr = HAL_GetTick();
        res = f_write(&s_audio_file, data, len, &bw);
        write_ms = HAL_GetTick() - twr;
        g_sd_audio_diag.write_ms_last = write_ms;
        if (write_ms > g_sd_audio_diag.write_ms_max) g_sd_audio_diag.write_ms_max = write_ms;
#endif
    } else { res = FR_OK; bw = len; }

    hold_ms = HAL_GetTick() - th;
    g_sd_audio_diag.hold_ms_last = hold_ms;
    if (hold_ms > g_sd_audio_diag.hold_ms_max) g_sd_audio_diag.hold_ms_max = hold_ms;

    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }

    {
        uint32_t dt = HAL_GetTick() - t0;
        if (dt > s_audio_max_write_ms) {
            s_audio_max_write_ms = dt;
        }
    }

    if (res == FR_OK && bw == len) {
        s_audio_bytes += bw;
        g_ecg_rec.mic_bytes = s_audio_bytes;
        g_sd_audio_diag.bytes_written += bw;
        return FR_OK;
    }

    g_sd_audio_diag.write_error++;
    return (res == FR_OK) ? FR_DISK_ERR : res;
}

static uint8_t audio_open_file(uint32_t seq)
{
    char path[32];
    uint8_t hdr[44];
    UINT bw = 0;
    FRESULT res;

    if (!RECORD_AUDIO_REOPEN_SAME_SEQ_ALLOWED && s_audio_failed_seq == seq) {
        Safe_USB_Printf("[MIC][ERR] refuse reopen same seq=%lu after runtime failure\r\n",
                        (unsigned long)seq);
        return 0;
    }

    if (g_session.created) {
        snprintf(path, sizeof(path), "%s/audio.wav", g_session.session_dir);
    } else {
        snprintf(path, sizeof(path), "0:/mic_%03lu.wav", (unsigned long)seq);
    }
#if RECORD_TEST_AUDIO_WRITE_ONLY_NO_SYNC
    /* F2: skip WAV header entirely, just open raw file */
    (void)hdr;
    if (Mtx_SDCardHandle != NULL) {
        if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(20)) != osOK) {
            Safe_USB_Printf("[MIC][ERR] mutex timeout before open\r\n");
            return 0;
        }
    }
    res = f_open(&s_audio_file, path, FA_CREATE_ALWAYS | FA_WRITE);
    { char abp[64]; snprintf(abp,sizeof(abp),"%s/audio_blocks.csv",g_session.session_dir);
      if(f_open(&s_audio_blocks_file,abp,FA_CREATE_ALWAYS|FA_WRITE)==FR_OK){
        const char *ah="tick_ms,byte_offset,byte_count,block_index\r\n"; UINT aw;
        f_write(&s_audio_blocks_file,ah,(UINT)strlen(ah),&aw); s_audio_blocks_open=1; } }
    bw = 0;
    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }
    if (res != FR_OK) {
#else
    build_wav_header(hdr, 0, AUDIO_SAMPLE_RATE_HZ);

    if (Mtx_SDCardHandle != NULL) {
        if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(20)) != osOK) {
            Safe_USB_Printf("[MIC][ERR] mutex timeout before open\r\n");
            return 0;
        }
    }

    res = f_open(&s_audio_file, path, FA_CREATE_ALWAYS | FA_WRITE);
    if (res == FR_OK) {
        res = f_write(&s_audio_file, hdr, sizeof(hdr), &bw);
    }
    if (res == FR_OK && bw == sizeof(hdr)) {
        res = f_sync(&s_audio_file);
    }

    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }

    if (res != FR_OK || bw != sizeof(hdr)) {
#endif
        Safe_USB_Printf("[MIC][ERR] open/header res=%d bw=%lu file=%s\r\n",
                        res, (unsigned long)bw, path);
        g_ecg_rec.mic_write_errors++;
        return 0;
    }

    s_audio_file_open = 1;
    s_audio_bytes = 0;
    s_audio_halves = 0;
    s_audio_dma_drops = 0;
    s_audio_mutex_timeouts = 0;
    s_audio_max_write_ms = 0;
    s_audio_half_irq_count = 0;
    s_audio_min_pcm = 32767;
    s_audio_max_pcm = -32768;
    g_ecg_rec.mic_file_open_tick = HAL_GetTick();
    g_ecg_rec.mic_bytes = 0;
    g_ecg_rec.mic_halves = 0;
    g_ecg_rec.mic_drops = 0;
    g_ecg_rec.mic_write_errors = 0;
    Safe_USB_Printf("[MIC] open ok file=%s\r\n", path);
    return 1;
}

static void audio_request_session_stop_on_error(const char *reason)
{
    s_audio_failed_seq = g_ecg_rec.file_seq;
    if (RECORD_AUDIO_ERROR_REQUESTS_STOP &&
        g_ecg_rec.state == ECG_REC_RECORDING) {
        SD_DebugLog_WriteLine(reason);
        g_ecg_rec.request_stop = 1;
    }
}

static void audio_close_file(void)
{
    uint8_t hdr[44];
    FRESULT seek_res = FR_OK;
    FRESULT write_res = FR_OK;
    FRESULT sync_res = FR_OK;
    FRESULT close_res = FR_OK;
    UINT bw = 0;

    if (!s_audio_file_open) {
        return;
    }

#if RECORD_TEST_AUDIO_WRITE_ONLY_NO_SYNC
    /* F2: skip header update, just close */
    (void)hdr;
    if (Mtx_SDCardHandle != NULL) {
        if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(20)) != osOK) {
            s_audio_mutex_timeouts++;
            s_audio_file_open = 0;
            return;
        }
    }
    AudioStallCapture_LogAll();
    close_res = f_close(&s_audio_file); if(s_audio_blocks_open){f_close(&s_audio_blocks_file);s_audio_blocks_open=0;}
#else
    build_wav_header(hdr, s_audio_bytes, RECORD_MIC_SAMPLE_RATE_HZ);

    if (Mtx_SDCardHandle != NULL) {
        if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(20)) != osOK) {
            s_audio_mutex_timeouts++;
            s_audio_file_open = 0;
            return;
        }
    }

    seek_res = f_lseek(&s_audio_file, 0);
    if (seek_res == FR_OK) {
        write_res = f_write(&s_audio_file, hdr, sizeof(hdr), &bw);
    }
#if RECORD_TEST_AUDIO_NO_PERIODIC_SYNC
    /* F3: only sync at close, not periodically */
    sync_res = f_sync(&s_audio_file);
#else
    sync_res = f_sync(&s_audio_file);
#endif
    close_res = f_close(&s_audio_file); if(s_audio_blocks_open){f_close(&s_audio_blocks_file);s_audio_blocks_open=0;}
#endif

    if (Mtx_SDCardHandle != NULL) {
        osMutexRelease(Mtx_SDCardHandle);
    }

    s_audio_file_open = 0;
    g_ecg_rec.mic_stop_tick = HAL_GetTick();
    g_ecg_rec.mic_bytes = s_audio_bytes;
    g_ecg_rec.mic_halves = s_audio_halves;
    audio_update_drop_count();
    {
        char stats[192];
        int n = snprintf(stats, sizeof(stats),
                         "MIC_STATS,bytes=%lu,halves=%lu,irq_halves=%lu,missed_halves=%lu,drops=%lu,timeout=%lu,maxwr=%lu,pcm_min=%d,pcm_max=%d",
                         (unsigned long)s_audio_bytes,
                         (unsigned long)s_audio_halves,
                         (unsigned long)s_audio_half_irq_count,
                         (unsigned long)audio_missed_half_count(),
                         (unsigned long)g_ecg_rec.mic_drops,
                         (unsigned long)s_audio_mutex_timeouts,
                         (unsigned long)s_audio_max_write_ms,
                         (int)s_audio_min_pcm,
                         (int)s_audio_max_pcm);
        if (n > 0 && n < (int)sizeof(stats)) {
            SD_DebugLog_WriteLine(stats);
        }
    }
    Safe_USB_Printf("[MIC] close seek=%d write=%d bw=%lu sync=%d close=%d bytes=%lu halves=%lu drops=%lu timeout=%lu maxwr=%lu\r\n",
                    seek_res, write_res, (unsigned long)bw, sync_res, close_res,
                    (unsigned long)s_audio_bytes,
                    (unsigned long)s_audio_halves,
                    (unsigned long)s_audio_dma_drops,
                    (unsigned long)s_audio_mutex_timeouts,
                    (unsigned long)s_audio_max_write_ms);
}

static int16_t sai_word_to_pcm16(uint32_t word)
{
    return AUDIO_SAI24_TO_PCM16(word);
}

static uint8_t audio_write_half(uint32_t *src, uint32_t words)
{
    uint32_t in = 0;
    uint32_t out_samples = 0;
    uint32_t min_raw = 0xffffffffU;
    uint32_t max_raw = 0;
    int16_t min_pcm = 32767;
    int16_t max_pcm = -32768;

    while (in + 1U < words && out_samples < AUDIO_PCM_HALF_SAMPLES) {
        uint32_t raw = src[in];
        int16_t pcm = sai_word_to_pcm16(raw);

        if (raw < min_raw) min_raw = raw;
        if (raw > max_raw) max_raw = raw;
        if (pcm < min_pcm) min_pcm = pcm;
        if (pcm > max_pcm) max_pcm = pcm;
        if (pcm < s_audio_min_pcm) s_audio_min_pcm = pcm;
        if (pcm > s_audio_max_pcm) s_audio_max_pcm = pcm;

        s_pcm_half[out_samples++] = pcm;
        in += 2U;
    }

        if (out_samples > 0U) {
        FRESULT res;
#if RECORD_FIX_AUDIO_NONBLOCKING_DISCARD
        /* P3: aggregate every 2 half-buffers to reduce f_write frequency */
        static uint32_t p3_agg_count = 0;
        p3_agg_count++;
        if (p3_agg_count >= 2U) {
            p3_agg_count = 0;
            res = audio_write_locked(s_pcm_half, out_samples * sizeof(int16_t));
            if(s_audio_blocks_open){char al[64];int an=snprintf(al,sizeof(al),"%lu,%lu,%u,%lu\r\n",(unsigned long)HAL_GetTick(),(unsigned long)s_audio_bytes,(unsigned)(out_samples*sizeof(int16_t)),(unsigned long)s_audio_blocks_seq++);if(an>0){UINT aw;f_write(&s_audio_blocks_file,al,(UINT)an,&aw);}}
        } else {
            res = FR_OK;  /* skip write, pretend success */
        }
#else
        res = audio_write_locked(s_pcm_half, out_samples * sizeof(int16_t));
            if(s_audio_blocks_open){char al[64];int an=snprintf(al,sizeof(al),"%lu,%lu,%u,%lu\r\n",(unsigned long)HAL_GetTick(),(unsigned long)s_audio_bytes,(unsigned)(out_samples*sizeof(int16_t)),(unsigned long)s_audio_blocks_seq++);if(an>0){UINT aw;f_write(&s_audio_blocks_file,al,(UINT)an,&aw);}}
#endif
        if (res != FR_OK) {
            g_ecg_rec.mic_write_errors++;
            Safe_USB_Printf("[MIC][ERR] write res=%d bytes=%lu\r\n",
                            res, (unsigned long)s_audio_bytes);
            audio_request_session_stop_on_error("MIC_ERROR_WRITE_REQUEST_STOP");
            return 0;
        }
    }

    s_audio_halves++;
    g_ecg_rec.mic_halves = s_audio_halves;
    audio_update_drop_count();

    if ((s_audio_halves % AUDIO_SYNC_EVERY_HALVES) == 0U) {
#if RECORD_TEST_AUDIO_WRITE_ONLY_NO_SYNC || RECORD_TEST_AUDIO_NO_PERIODIC_SYNC || RECORD_FIX_AUDIO_NONBLOCKING_DISCARD
        /* F2/F3: skip periodic header update + f_sync during recording */
        FRESULT sync_res = FR_OK;
#else
        FRESULT sync_res = FR_OK;

        if (Mtx_SDCardHandle != NULL) {
            if (osMutexAcquire(Mtx_SDCardHandle, pdMS_TO_TICKS(20)) != osOK) {
                s_audio_mutex_timeouts++;
                g_ecg_rec.mic_drops = s_audio_dma_drops + s_audio_mutex_timeouts;
                audio_request_session_stop_on_error("MIC_ERROR_SYNC_MUTEX_TIMEOUT_REQUEST_STOP");
                return 0;
            }
        }

        sync_res = audio_update_header_and_sync_locked();
#endif

#if !RECORD_TEST_AUDIO_WRITE_ONLY_NO_SYNC && !RECORD_TEST_AUDIO_NO_PERIODIC_SYNC && !RECORD_FIX_AUDIO_NONBLOCKING_DISCARD
        if (Mtx_SDCardHandle != NULL) {
            osMutexRelease(Mtx_SDCardHandle);
        }
#endif

        if (sync_res != FR_OK) {
            g_ecg_rec.mic_write_errors++;
            Safe_USB_Printf("[MIC][ERR] periodic sync res=%d bytes=%lu\r\n",
                            sync_res, (unsigned long)s_audio_bytes);
            audio_request_session_stop_on_error("MIC_ERROR_SYNC_REQUEST_STOP");
            return 0;
        }
    }
#if AUDIO_USB_VERBOSE
    if ((s_audio_halves % 8U) == 0U) {
        Safe_USB_Printf("[MIC] write halves=%lu bytes=%lu raw=%08lx..%08lx pcm=%d..%d drops=%lu\r\n",
                        (unsigned long)s_audio_halves,
                        (unsigned long)s_audio_bytes,
                        (unsigned long)min_raw,
                        (unsigned long)max_raw,
                        (int)min_pcm,
                        (int)max_pcm,
                        (unsigned long)s_audio_dma_drops);
    }
#endif

    return 1;
}

static void audio_notify_from_isr(uint32_t bit)
{
    BaseType_t higher_priority_woken = pdFALSE;

    if (s_audio_task_handle != NULL) {
        if (xTaskNotifyFromISR(s_audio_task_handle, bit, eSetBits,
                               &higher_priority_woken) != pdPASS) {
            s_audio_dma_drops++;
        }
        s_audio_half_irq_count++;
        portYIELD_FROM_ISR(higher_priority_woken);
    }
}

void HAL_SAI_RxHalfCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == &hsai_BlockA1) {
        audio_notify_from_isr(AUDIO_NOTIFY_HALF0);
    }
}

void HAL_SAI_RxCpltCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == &hsai_BlockA1) {
        audio_notify_from_isr(AUDIO_NOTIFY_HALF1);
    }
}

void HAL_SAI_ErrorCallback(SAI_HandleTypeDef *hsai)
{
    if (hsai == &hsai_BlockA1) {
        s_audio_dma_drops++;
    }
}

uint8_t AudioRecorder_IsActive(void)
{
    return (uint8_t)(s_audio_recording_active || s_audio_file_open);
}

void AudioRecorder_Task(void *argument)
{
    (void)argument;
    s_audio_task_handle = xTaskGetCurrentTaskHandle();

    HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_RESET);

    for (;;) {
        while (g_ecg_rec.state != ECG_REC_RECORDING || !g_ecg_rec.sd_file_opened) {
            osDelay(20);
        }

        uint32_t seq = g_ecg_rec.file_seq;
        uint32_t notify = 0;

        /* --- Session latch: prevent hot-loop re-entry --- */
        static uint32_t handled_seq = 0xFFFFFFFFU;
        if (g_ecg_rec.sd_file_opened == 0U) {
            osDelay(20);
            continue;
        }
        if (handled_seq == g_ecg_rec.file_seq) {
            osDelay(20);
            continue;
        }
        handled_seq = g_ecg_rec.file_seq;
        if (g_ecg_rec.state != ECG_REC_RECORDING) {
            handled_seq = 0xFFFFFFFFU;
        }
        /* --- End session latch --- */

        s_audio_recording_active = 1;
        g_audio_run_diag.audio_state = 2;
        g_audio_run_diag.sai_dma_started++;
        AudioRunDiag_Reset();
        g_audio_run_diag.audio_state = 2;
        g_audio_run_diag.sai_dma_started++;
        g_ecg_rec.mic_power_tick = HAL_GetTick();
        if (RECORD_DIAG_AUDIO_EN_MIC_ON) { HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_SET); }
        osDelay(50);

        if (RECORD_DIAG_AUDIO_MIC_SD_WRITE && !RECORD_DIAG_DISABLE_ALL_SD_WRITES && !audio_open_file(seq)) {
            s_audio_recording_active = 0;
            HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_RESET);
            if (g_ecg_rec.state == ECG_REC_RECORDING &&
                s_audio_failed_seq == seq &&
                RECORD_AUDIO_ERROR_REQUESTS_STOP) {
                g_ecg_rec.request_stop = 1;
            }
            osDelay(500);
            continue;
        }

        {
            uint32_t wait0 = HAL_GetTick();
            while (g_ecg_rec.state == ECG_REC_RECORDING &&
                   g_ecg_rec.ecg_stream_start_tick == 0U &&
                   (HAL_GetTick() - wait0) < 3000U) {
                osDelay(1);
            }
        }

        if (g_ecg_rec.state != ECG_REC_RECORDING ||
            g_ecg_rec.ecg_stream_start_tick == 0U) {
            if (!RECORD_DIAG_DISABLE_ALL_SD_WRITES) { audio_close_file(); }
            s_audio_recording_active = 0;
            HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_RESET);
            osDelay(500);
            continue;
        }

        memset(s_audio_dma_buf, 0, sizeof(s_audio_dma_buf));
        while (xTaskNotifyWait(0, 0xffffffffUL, &notify, 0) == pdTRUE) {
            (void)notify;
        }

        HAL_StatusTypeDef ret = HAL_OK;
        if (RECORD_DIAG_AUDIO_SAI_DMA) {
            ret = HAL_SAI_Receive_DMA(&hsai_BlockA1,
                                                    (uint8_t *)s_audio_dma_buf,
                                                    AUDIO_DMA_WORDS);
        }
        g_ecg_rec.mic_dma_start_tick = HAL_GetTick();
        /* priority stays at creation level, below SensorTask */
        Safe_USB_Printf("[MIC] dma_start ret=%d words=%lu bytes=%lu\r\n",
                        ret, (unsigned long)AUDIO_DMA_WORDS,
                        (unsigned long)AUDIO_DMA_BYTES);

        if (ret != HAL_OK) {
            if (!RECORD_DIAG_DISABLE_ALL_SD_WRITES) { audio_close_file(); }
            s_audio_recording_active = 0;
            HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_RESET);
            osDelay(500);
            continue;
        }

        if (RECORD_DIAG_AUDIO_SAI_DMA) {
            while (g_ecg_rec.state == ECG_REC_RECORDING ||
               g_ecg_rec.state == ECG_REC_STOPPING) {
            if (xTaskNotifyWait(0, 0xffffffffUL, &notify,
                                pdMS_TO_TICKS(200)) == pdTRUE) {
                if ((notify & AUDIO_NOTIFY_HALF0) != 0U) {
                    if (g_ecg_rec.mic_first_half_tick == 0U) {
                        g_ecg_rec.mic_first_half_tick = HAL_GetTick();
                    }
                    if (RECORD_DIAG_AUDIO_MIC_PACK && !audio_write_half(&s_audio_dma_buf[0], AUDIO_DMA_HALF_WORDS)) {
                        break;
                    }
                }
                if ((notify & AUDIO_NOTIFY_HALF1) != 0U) {
                    if (g_ecg_rec.mic_first_half_tick == 0U) {
                        g_ecg_rec.mic_first_half_tick = HAL_GetTick();
                    }
                    if (RECORD_DIAG_AUDIO_MIC_PACK && !audio_write_half(&s_audio_dma_buf[AUDIO_DMA_HALF_WORDS],
                                          AUDIO_DMA_HALF_WORDS)) {
                        break;
                    }
                }
            }
        }

        }
        HAL_SAI_DMAStop(&hsai_BlockA1);
        if (!RECORD_DIAG_DISABLE_ALL_SD_WRITES) { audio_close_file(); }
        s_audio_recording_active = 0;
        HAL_GPIO_WritePin(EN_MIC_GPIO_Port, EN_MIC_Pin, GPIO_PIN_RESET);

        while (g_ecg_rec.state == ECG_REC_STOPPING ||
               g_ecg_rec.state == ECG_REC_STOPPED) {
            osDelay(50);
            if (g_ecg_rec.state == ECG_REC_IDLE || g_ecg_rec.state == ECG_REC_ERROR) {
                break;
            }
        }
    }
}











/* ===== AUDIO_RUN 璇婃柇瀹炵幇 ===== */

void AudioRunDiag_Reset(void)
{
    memset(&g_audio_run_diag, 0, sizeof(g_audio_run_diag));
}

void AudioRunDiag_LogSnapshot(void)
{
    char line[256];
    snprintf(line, sizeof(line),
             "AUDIO_RUN alv=%lu st=%lu dma=%lu hc=%lu blk=%lu/%lu/%lu cap=%lu wr=%lu "
             "dma_t=%lu wr_t=%lu err=%lu mtx=%lu wav=%lu/%lu",
             (unsigned long)g_audio_run_diag.audio_task_alive,
             (unsigned long)g_audio_run_diag.audio_state,
             (unsigned long)g_audio_run_diag.sai_dma_started,
             (unsigned long)g_audio_run_diag.dma_half_count,
             (unsigned long)g_audio_run_diag.blocks_in,
             (unsigned long)g_audio_run_diag.blocks_written,
             (unsigned long)g_audio_run_diag.blocks_dropped,
             (unsigned long)g_audio_run_diag.bytes_captured,
             (unsigned long)g_audio_run_diag.bytes_written,
             (unsigned long)g_audio_run_diag.last_dma_tick,
             (unsigned long)g_audio_run_diag.last_write_tick,
             (unsigned long)g_audio_run_diag.last_error_code,
             (unsigned long)g_audio_run_diag.mutex_timeouts,
             (unsigned long)g_audio_run_diag.wav_file_open,
             (unsigned long)g_audio_run_diag.wav_data_bytes);
    SD_DebugLog_WriteLine(line);
}

/* ===== AUDIO_STALL Capture ===== */

void AudioStallCapture_Snapshot(uint32_t reason, uint32_t halves, uint32_t timeout_count)
{
    uint32_t idx = g_audio_stall_capture_count;
    if (idx >= AUDIO_STALL_CAPTURE_COUNT) return;
    AudioStallCapture_t *cap = &g_audio_stall_captures[idx];
    cap->capture_tick = HAL_GetTick();
    cap->capture_seq = idx + 1;
    cap->stall_reason = reason;
    cap->s_audio_halves = halves;
    cap->stall_timeout_count = timeout_count;
    cap->dma_ccr = DMA2_Channel6->CCR;
    cap->dma_cndtr = DMA2_Channel6->CNDTR;
    cap->dma_isr_chan = (DMA2->ISR >> 16U) & 0x1FU;
    cap->dmamux_ccr = 0;
    cap->sai_sr = SAI1_Block_A->SR;
    cap->sai_cr1 = SAI1_Block_A->CR1;
    cap->sai_cr2 = SAI1_Block_A->CR2;
    cap->hal_sai_state = (uint32_t)hsai_BlockA1.State;
    cap->hal_sai_error = hsai_BlockA1.ErrorCode;
    cap->hal_dma_state = (uint32_t)hdma_sai1_a.State;
    g_audio_stall_capture_count = idx + 1;
}

void AudioStallCapture_LogAll(void)
{
    uint32_t count = g_audio_stall_capture_count;
    if (count == 0U) return;

    Safe_USB_Printf("AUDIO_STALL count=%lu\r\n", (unsigned long)count);
    for (uint32_t i = 0U; i < count; i++) {
        AudioStallCapture_t *cap = &g_audio_stall_captures[i];
        char line[320];
        int n = snprintf(line, sizeof(line),
                 "AUDIO_STALL #%lu tick=%lu reason=%lu halves=%lu to_cnt=%lu "
                 "dma_ccr=0x%08lx dma_cndtr=%lu dma_isr=0x%02lx dmamux=0x%08lx "
                 "sai_sr=0x%08lx sai_cr1=0x%08lx sai_cr2=0x%08lx "
                 "hal_sai=%lu hal_sai_err=0x%08lx hal_dma=%lu\r\n",
                 (unsigned long)cap->capture_seq,
                 (unsigned long)cap->capture_tick,
                 (unsigned long)cap->stall_reason,
                 (unsigned long)cap->s_audio_halves,
                 (unsigned long)cap->stall_timeout_count,
                 (unsigned long)cap->dma_ccr,
                 (unsigned long)cap->dma_cndtr,
                 (unsigned long)cap->dma_isr_chan,
                 (unsigned long)cap->dmamux_ccr,
                 (unsigned long)cap->sai_sr,
                 (unsigned long)cap->sai_cr1,
                 (unsigned long)cap->sai_cr2,
                 (unsigned long)cap->hal_sai_state,
                 (unsigned long)cap->hal_sai_error,
                 (unsigned long)cap->hal_dma_state);
        Safe_USB_Printf("%s\r\n", line);
    }
}
