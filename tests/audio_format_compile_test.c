#include <stdint.h>

#include "audio_sample_format.h"

#define CHECK_AUDIO_FORMAT(name, expr) _Static_assert((expr), name)

CHECK_AUDIO_FORMAT("zero stays zero",
                   AUDIO_SAI24_TO_PCM16(0x00000000U) == 0);
CHECK_AUDIO_FORMAT("small positive keeps sign",
                   AUDIO_SAI24_TO_PCM16(0x00000100U) == 1);
CHECK_AUDIO_FORMAT("largest positive 24-bit maps to int16 max",
                   AUDIO_SAI24_TO_PCM16(0x007FFFFFU) == 32767);
CHECK_AUDIO_FORMAT("negative full-scale 24-bit maps to int16 min",
                   AUDIO_SAI24_TO_PCM16(0x00800000U) == -32768);
CHECK_AUDIO_FORMAT("minus one 24-bit stays negative",
                   AUDIO_SAI24_TO_PCM16(0x00FFFFFFU) == -1);

int main(void)
{
    return 0;
}
