#ifndef __AUDIO_WAV_FORMAT_H__
#define __AUDIO_WAV_FORMAT_H__

#include <stdint.h>

#define AUDIO_WAV_PCM16_BYTES_PER_SAMPLE 2U

#define AUDIO_WAV_SAMPLE_COUNT_FROM_BYTES(data_bytes_) \
    ((uint32_t)((data_bytes_) / AUDIO_WAV_PCM16_BYTES_PER_SAMPLE))

#define AUDIO_WAV_MEASURED_RATE_HZ(data_bytes_, duration_ms_) \
    ((uint32_t)(((((uint64_t)AUDIO_WAV_SAMPLE_COUNT_FROM_BYTES(data_bytes_)) * 1000ULL) + \
                 ((uint64_t)(duration_ms_) / 2ULL)) / \
                ((uint64_t)(duration_ms_))))

#define AUDIO_WAV_EFFECTIVE_RATE_HZ(data_bytes_, duration_ms_, nominal_hz_, min_hz_, max_hz_) \
    (((duration_ms_) < 1000U || AUDIO_WAV_SAMPLE_COUNT_FROM_BYTES(data_bytes_) == 0U) ? \
        (uint32_t)(nominal_hz_) : \
        ((AUDIO_WAV_MEASURED_RATE_HZ((data_bytes_), (duration_ms_)) < (uint32_t)(min_hz_) || \
          AUDIO_WAV_MEASURED_RATE_HZ((data_bytes_), (duration_ms_)) > (uint32_t)(max_hz_)) ? \
            (uint32_t)(nominal_hz_) : \
            AUDIO_WAV_MEASURED_RATE_HZ((data_bytes_), (duration_ms_))))

#endif /* __AUDIO_WAV_FORMAT_H__ */
