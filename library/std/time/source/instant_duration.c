#include "r_std_time.h"

#include "r_library_duration_internal.h"

RStdTimeDurationTimeResult r_std_time_instant_duration(RStdTimeInstant later,
                                                       RStdTimeInstant earlier) {
    return r_library_internal_time_instant_duration(later, earlier);
}
