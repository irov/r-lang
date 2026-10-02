#ifndef R_LIBRARY_DURATION_INTERNAL_H
#define R_LIBRARY_DURATION_INTERNAL_H

#include "r_std_time.h"

RStdTimeDurationResult
r_library_internal_duration_add(RStdTimeDuration left, RStdTimeDuration right, _Bool subtract);
RStdTimeDurationResult r_library_internal_duration_multiply(RStdTimeDuration value, int64_t factor);
RStdTimeInstantResult r_library_internal_time_instant_add(RStdTimeInstant base,
                                                          RStdTimeDuration delta);
RStdTimeDurationTimeResult r_library_internal_time_instant_duration(RStdTimeInstant later,
                                                                    RStdTimeInstant earlier);

#endif
