#include "r_std_time.h"

#include "r_library_duration_internal.h"

RStdTimeDurationResult r_std_time_duration_sub(RStdTimeDuration left, RStdTimeDuration right) {
    return r_library_internal_duration_add(left, right, 1);
}
