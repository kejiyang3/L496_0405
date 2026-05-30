#include "audio_wav_format.h"

#define CHECK_AUDIO_WAV_RATE(name, expr) _Static_assert((expr), name)

CHECK_AUDIO_WAV_RATE("short empty recording keeps nominal rate",
                     AUDIO_WAV_EFFECTIVE_RATE_HZ(0U, 0U, 16000U, 1000U, 48000U) == 16000U);

CHECK_AUDIO_WAV_RATE("long 10 minute recording rate calculation must not overflow",
                     AUDIO_WAV_EFFECTIVE_RATE_HZ(22405120U, 599207U, 16000U, 1000U, 48000U) == 18696U);

CHECK_AUDIO_WAV_RATE("out of range measured rate falls back to nominal",
                     AUDIO_WAV_EFFECTIVE_RATE_HZ(22405120U, 12000000U, 16000U, 1000U, 48000U) == 16000U);

int main(void)
{
    return 0;
}
