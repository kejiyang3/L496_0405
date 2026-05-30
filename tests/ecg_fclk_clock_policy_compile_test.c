#include "main.h"

#ifndef ECG_FCLK_MCO_GPIO_SPEED
#error "ECG_FCLK_MCO_GPIO_SPEED must be defined"
#endif

#ifndef ECG_LSE_DRIVE
#error "ECG_LSE_DRIVE must be defined"
#endif

_Static_assert(ECG_FCLK_MCO_GPIO_SPEED == GPIO_SPEED_FREQ_VERY_HIGH,
               "MAX30003 FCLK MCO pin must use very high GPIO speed");

_Static_assert(ECG_LSE_DRIVE == RCC_LSEDRIVE_HIGH,
               "MAX30003 FCLK LSE source must use high drive");

int main(void)
{
    return 0;
}
