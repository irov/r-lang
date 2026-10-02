#ifndef R_LIBRARY_SLEEP_INTERNAL_H
#define R_LIBRARY_SLEEP_INTERNAL_H

#include "r_std_time.h"

RStdTimeSleepStartResult r_library_internal_time_sleep_for(RStdTimeDuration duration);
RStdTimeSleepStartResult r_library_internal_time_sleep_until(RStdTimeInstant when);

#endif
