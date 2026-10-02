#include "r_std_time.h"

#include "r_library_clock_internal.h"

RStdTimeSystemTimeResult r_std_time_system_now(void) {
    return r_library_internal_time_system_now(r_library_internal_time_darwin_clock_hooks());
}
