#include "r_std_time.h"

#include "r_library_duration_internal.h"

RStdTimeDurationResult r_std_time_duration_multiply(RStdTimeDuration value, int64_t factor) {
    return r_library_internal_duration_multiply(value, factor);
}
