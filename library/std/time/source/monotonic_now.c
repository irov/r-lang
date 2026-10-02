#include "r_std_time.h"

#include "r_library_clock_internal.h"

RStdTimeInstantResult r_std_time_monotonic_now(void) {
    return r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
}
