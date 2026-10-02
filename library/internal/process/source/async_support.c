#include "r_library_process_internal.h"

#include "r_library_clock_internal.h"
#include "r_library_duration_internal.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>

static int compare_instant(RStdTimeInstant left, RStdTimeInstant right) {
    if (left.storage_seconds != right.storage_seconds) {
        return left.storage_seconds < right.storage_seconds ? -1 : 1;
    }
    if (left.storage_nanoseconds != right.storage_nanoseconds) {
        return left.storage_nanoseconds < right.storage_nanoseconds ? -1 : 1;
    }
    return 0;
}

RStdProcessError r_library_internal_process_control_error_from_native(int native_error) {
    RStdProcessErrorCode code = R_STD_PROCESS_ERROR_OTHER;

    if (native_error == ESRCH) {
        code = R_STD_PROCESS_ERROR_NOT_RUNNING;
    } else if (native_error == EACCES || native_error == EPERM) {
        code = R_STD_PROCESS_ERROR_PERMISSION_DENIED;
    } else if (native_error == EAGAIN || native_error == ENOMEM || native_error == EMFILE ||
               native_error == ENFILE || native_error == ENOSPC) {
        code = R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED;
#if defined(ENOTSUP)
    } else if (native_error == ENOTSUP) {
        code = R_STD_PROCESS_ERROR_UNSUPPORTED;
#endif
    }
    return r_library_internal_process_error(code, (int64_t)native_error);
}

RLibraryProcessDeadlineStatus r_library_internal_process_deadline_timeout(
    RStdProcessDeadline deadline, uint64_t *timeout_nanoseconds, RStdProcessError *error) {
    const uint64_t nanoseconds_per_second = UINT64_C(1000000000);
    RStdTimeInstantResult now;
    RStdTimeDurationTimeResult remaining;
    uint64_t seconds;

    if (timeout_nanoseconds == NULL || error == NULL) {
        return R_LIBRARY_PROCESS_DEADLINE_ERROR;
    }
    *timeout_nanoseconds = UINT64_C(0);
    *error = (RStdProcessError){0};
    if (!deadline.has_value) {
        return R_LIBRARY_PROCESS_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        *error = r_library_internal_process_error(R_STD_PROCESS_ERROR_OTHER, INT64_C(0));
        return R_LIBRARY_PROCESS_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        *error = r_library_internal_process_error(R_STD_PROCESS_ERROR_OTHER, now.error.native_code);
        return R_LIBRARY_PROCESS_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        *error = r_library_internal_process_error(R_STD_PROCESS_ERROR_TIMED_OUT, INT64_C(0));
        return R_LIBRARY_PROCESS_DEADLINE_EXPIRED;
    }
    remaining = r_library_internal_time_instant_duration(deadline.value, now.value);
    if (!remaining.is_ok || remaining.value.seconds < 0) {
        *error = r_library_internal_process_error(R_STD_PROCESS_ERROR_OTHER,
                                                  remaining.error.native_code);
        return R_LIBRARY_PROCESS_DEADLINE_ERROR;
    }
    seconds = (uint64_t)remaining.value.seconds;
    if (seconds > (uint64_t)INT64_MAX / nanoseconds_per_second) {
        *timeout_nanoseconds = (uint64_t)INT64_MAX;
    } else {
        const uint64_t whole = seconds * nanoseconds_per_second;
        const uint64_t fraction = (uint64_t)remaining.value.nanoseconds;

        *timeout_nanoseconds =
            fraction > (uint64_t)INT64_MAX - whole ? (uint64_t)INT64_MAX : whole + fraction;
    }
    if (*timeout_nanoseconds == UINT64_C(0)) {
        *timeout_nanoseconds = UINT64_C(1);
    }
    return R_LIBRARY_PROCESS_DEADLINE_READY;
}
