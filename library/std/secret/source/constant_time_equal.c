#include "r_std_secret.h"

_Bool r_std_secret_constant_time_equal(RStdSecretSlice left, RStdSecretSlice right) {
    /* R-SLIB-SECRET-0004: the accumulator is volatile so no iteration can be skipped. */
    volatile unsigned int difference = 0U;
    size_t index;

    if (left.length != right.length) {
        return 0;
    }
    for (index = 0U; index < left.length; index += 1U) {
        difference |= (unsigned int)(left.data[index] ^ right.data[index]);
    }
    return difference == 0U;
}
