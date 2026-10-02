#include "r_std_time.h"

#include "r_library_duration_internal.h"

RStdTimeSystemTimeResult r_std_time_system_add(RStdTimeSystemTime base, RStdTimeDuration delta) {
    RStdTimeSystemTimeResult result = {0};
    RStdTimeDurationResult sum = r_library_internal_duration_add(
        (RStdTimeDuration){base.unix_seconds, base.nanoseconds}, delta, 0);

    if (!sum.is_ok) {
        result.error.code = sum.error == R_STD_TIME_DURATION_ERROR_INVALID_NANOSECONDS
                                ? R_STD_TIME_ERROR_INVALID_VALUE
                                : R_STD_TIME_ERROR_OVERFLOW;
        return result;
    }
    result.is_ok = 1;
    result.value.unix_seconds = sum.value.seconds;
    result.value.nanoseconds = sum.value.nanoseconds;
    return result;
}
