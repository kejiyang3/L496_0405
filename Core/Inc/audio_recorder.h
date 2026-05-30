#ifndef __AUDIO_RECORDER_H__
#define __AUDIO_RECORDER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

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

