#ifndef R_LIBRARY_CLOCK_INTERNAL_H
#define R_LIBRARY_CLOCK_INTERNAL_H

#include "r_std_time.h"

#include <stdint.h>

typedef int64_t (*RLibraryTimeReadContinuous)(void *context,
                                              uint64_t *ticks,
                                              uint32_t *numerator,
                                              uint32_t *denominator);
typedef int64_t (*RLibraryTimeReadRealtime)(void *context, int64_t *seconds, int64_t *nanoseconds);
typedef _Bool (*RLibraryTimeStatusUnavailable)(int64_t native_code);

typedef struct RLibraryTimeClockHooks {
    void *context;
    RLibraryTimeReadContinuous read_continuous;
    RLibraryTimeReadRealtime read_realtime;
    RLibraryTimeStatusUnavailable continuous_status_unavailable;
    RLibraryTimeStatusUnavailable realtime_status_unavailable;
} RLibraryTimeClockHooks;

const RLibraryTimeClockHooks *r_library_internal_time_darwin_clock_hooks(void);
RStdTimeInstantResult r_library_internal_time_monotonic_now(const RLibraryTimeClockHooks *hooks);
RStdTimeSystemTimeResult r_library_internal_time_system_now(const RLibraryTimeClockHooks *hooks);
RStdTimeInstantResult
r_library_internal_time_instant_from_mach(uint64_t ticks, uint32_t numerator, uint32_t denominator);

#endif
