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

#ifdef __cplusplus
}
#endif

#endif /* __AUDIO_RECORDER_H__ */
