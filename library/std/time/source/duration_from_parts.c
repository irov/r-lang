#include "r_std_time.h"

RStdTimeDurationResult r_std_time_duration_from_parts(int64_t seconds, uint32_t nanoseconds) {
    RStdTimeDurationResult result = {0};

    if (nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        result.error = R_STD_TIME_DURATION_ERROR_INVALID_NANOSECONDS;
        return result;
    }
    result.is_ok = 1;
    result.value.seconds = seconds;
    result.value.nanoseconds = nanoseconds;
    return result;
}
