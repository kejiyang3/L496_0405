#ifndef __AUDIO_SAMPLE_FORMAT_H__
#define __AUDIO_SAMPLE_FORMAT_H__

#include <stdint.h>

/*
 * SAI 24-bit samples arrive in the low 24 bits of each DMA word.
 * Sign-extend bit 23 first, then reduce to 16-bit PCM.
 */
#define AUDIO_SAI24_TO_PCM16(word_) \
    ((int16_t)(((((int32_t)((uint32_t)(word_) & 0x00FFFFFFU)) ^ 0x00800000) - 0x00800000) >> 8))

#endif /* __AUDIO_SAMPLE_FORMAT_H__ */
