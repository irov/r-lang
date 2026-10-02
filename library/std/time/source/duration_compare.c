#include "r_std_time.h"

int32_t r_std_time_duration_compare(RStdTimeDuration left, RStdTimeDuration right) {
    if (left.seconds < right.seconds) {
        return -INT32_C(1);
    }
    if (left.seconds > right.seconds) {
        return INT32_C(1);
    }
    if (left.nanoseconds < right.nanoseconds) {
        return -INT32_C(1);
    }
    if (left.nanoseconds > right.nanoseconds) {
        return INT32_C(1);
    }
    return 0;
}
