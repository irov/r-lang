#include "r_std_time.h"

#include "r_library_duration_internal.h"

RStdTimeInstantResult r_std_time_instant_add(RStdTimeInstant base, RStdTimeDuration delta) {
    return r_library_internal_time_instant_add(base, delta);
}
