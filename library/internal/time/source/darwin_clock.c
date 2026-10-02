#include "r_library_clock_internal.h"

#include <errno.h>
#include <stdint.h>

#if defined(__APPLE__)
#include <mach/kern_return.h>
#include <mach/mach_time.h>
#include <time.h>
#endif

static int64_t
read_continuous(void *context, uint64_t *ticks, uint32_t *numerator, uint32_t *denominator) {
    (void)context;
#if defined(__APPLE__)
    mach_timebase_info_data_t timebase = {0};
    kern_return_t status = mach_timebase_info(&timebase);

    if (status != KERN_SUCCESS) {
        return (int64_t)status;
    }
    *ticks = mach_continuous_time();
    *numerator = timebase.numer;
    *denominator = timebase.denom;
    return INT64_C(0);
#else
    (void)ticks;
    (void)numerator;
    (void)denominator;
    return (int64_t)ENOSYS;
#endif
}

static int64_t read_realtime(void *context, int64_t *seconds, int64_t *nanoseconds) {
    (void)context;
#if defined(__APPLE__)
    struct timespec reading = {0};

    if (clock_gettime(CLOCK_REALTIME, &reading) != 0) {
        const int captured_errno = errno;

        return (int64_t)captured_errno;
    }
    *seconds = (int64_t)reading.tv_sec;
    *nanoseconds = (int64_t)reading.tv_nsec;
    return INT64_C(0);
#else
    (void)seconds;
    (void)nanoseconds;
    return (int64_t)ENOSYS;
#endif
}

static _Bool continuous_status_unavailable(int64_t native_code) {
#if defined(__APPLE__)
    return native_code == (int64_t)KERN_RESOURCE_SHORTAGE;
#else
    return native_code == (int64_t)ENOSYS;
#endif
}

static _Bool realtime_status_unavailable(int64_t native_code) {
    return (native_code == (int64_t)EAGAIN) || (native_code == (int64_t)EINTR) ||
           (native_code == (int64_t)ENOSYS);
}

const RLibraryTimeClockHooks *r_library_internal_time_darwin_clock_hooks(void) {
    static const RLibraryTimeClockHooks hooks = {
        NULL,
        read_continuous,
        read_realtime,
        continuous_status_unavailable,
        realtime_status_unavailable,
    };

    return &hooks;
}
