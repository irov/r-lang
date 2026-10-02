#include "r_std_time.h"

#include "r_library_sleep_internal.h"

RStdTimeSleepStartResult r_std_time_sleep_for(RStdTimeDuration duration) {
    return r_library_internal_time_sleep_for(duration);
}
