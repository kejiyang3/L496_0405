#include "max3003.h"

_Static_assert(MAX30003_NO_EINT_DRAIN_SAMPLES >= 16U,
               "ECG task must drain FIFO backlog even when STATUS.EINT is not latched");

_Static_assert(MAX30003_NO_EINT_DRAIN_SAMPLES <= FIFO_BURST_SIZE,
               "ECG no-EINT drain must stay bounded by one burst");

_Static_assert(MAX30003_ECG_TASK_USES_BURST_FIFO == 1,
               "ECG task must use burst FIFO reads to keep up during four-modal recording");

_Static_assert(MAX30003_ECG_FIFO_BURST == 0x20U,
               "MAX30003 ECG FIFO burst register must be 0x20");

_Static_assert(MAX30003_ECG_FIFO == 0x21U,
               "MAX30003 ECG FIFO normal register must be 0x21, not the burst address");

int main(void)
{
    return 0;
}
