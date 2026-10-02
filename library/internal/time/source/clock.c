#include "r_library_clock_internal.h"

#include <limits.h>
#include <stddef.h>
#include <stdint.h>

static RStdTimeError native_error(int64_t native_code, _Bool unavailable) {
    return (RStdTimeError){
        unavailable ? R_STD_TIME_ERROR_UNAVAILABLE : R_STD_TIME_ERROR_OTHER,
        native_code,
    };
}

static RStdTimeInstantResult instant_error(RStdTimeErrorCode code, int64_t native_code) {
    RStdTimeInstantResult result = {0};

    result.error = (RStdTimeError){code, native_code};
    return result;
}

RStdTimeInstantResult r_library_internal_time_instant_from_mach(uint64_t ticks,
                                                                uint32_t numerator,
                                                                uint32_t denominator) {
    const uint64_t nanoseconds_per_second = UINT64_C(1000000000);
    RStdTimeInstantResult result = {0};
    uint64_t quotient;
    uint64_t remainder;
    uint64_t quotient_seconds;
    uint64_t quotient_nanoseconds;
    uint64_t low_nanoseconds;
    uint64_t seconds;

    if ((numerator == 0U) || (denominator == 0U)) {
        return instant_error(R_STD_TIME_ERROR_OTHER, INT64_C(0));
    }
    quotient = ticks / (uint64_t)denominator;
    remainder = ticks % (uint64_t)denominator;
    quotient_seconds = quotient / nanoseconds_per_second;
    quotient_nanoseconds = quotient % nanoseconds_per_second;
    if ((quotient_seconds != 0U) &&
        ((uint64_t)numerator > ((uint64_t)INT64_MAX / quotient_seconds))) {
        return instant_error(R_STD_TIME_ERROR_OVERFLOW, INT64_C(0));
    }
    seconds = quotient_seconds * (uint64_t)numerator;
    low_nanoseconds = quotient_nanoseconds * (uint64_t)numerator;
    low_nanoseconds += (remainder * (uint64_t)numerator) / (uint64_t)denominator;
    if ((low_nanoseconds / nanoseconds_per_second) > ((uint64_t)INT64_MAX - seconds)) {
        return instant_error(R_STD_TIME_ERROR_OVERFLOW, INT64_C(0));
    }
    seconds += low_nanoseconds / nanoseconds_per_second;
    result.is_ok = 1;
    result.value.storage_seconds = (int64_t)seconds;
    result.value.storage_nanoseconds = (uint32_t)(low_nanoseconds % nanoseconds_per_second);
    return result;
}

RStdTimeInstantResult r_library_internal_time_monotonic_now(const RLibraryTimeClockHooks *hooks) {
    RStdTimeInstantResult result;
    uint64_t ticks = 0U;
    uint32_t numerator = 0U;
    uint32_t denominator = 0U;
    int64_t native_code;

    if ((hooks == NULL) || (hooks->read_continuous == NULL) ||
        (hooks->continuous_status_unavailable == NULL)) {
        return instant_error(R_STD_TIME_ERROR_OTHER, INT64_C(0));
    }
    native_code = hooks->read_continuous(hooks->context, &ticks, &numerator, &denominator);
    if (native_code != 0) {
        result = (RStdTimeInstantResult){0};
        result.error = native_error(native_code, hooks->continuous_status_unavailable(native_code));
        return result;
    }
    return r_library_internal_time_instant_from_mach(ticks, numerator, denominator);
}

RStdTimeSystemTimeResult r_library_internal_time_system_now(const RLibraryTimeClockHooks *hooks) {
    RStdTimeSystemTimeResult result = {0};
    int64_t seconds = 0;
    int64_t nanoseconds = 0;
    int64_t native_code;

    if ((hooks == NULL) || (hooks->read_realtime == NULL) ||
        (hooks->realtime_status_unavailable == NULL)) {
        result.error = (RStdTimeError){R_STD_TIME_ERROR_OTHER, INT64_C(0)};
        return result;
    }
    native_code = hooks->read_realtime(hooks->context, &seconds, &nanoseconds);
    if (native_code != 0) {
        result.error = native_error(native_code, hooks->realtime_status_unavailable(native_code));
        return result;
    }
    if ((nanoseconds < 0) || (nanoseconds >= (int64_t)R_STD_TIME_NANOSECONDS_PER_SECOND)) {
        result.error = (RStdTimeError){R_STD_TIME_ERROR_OTHER, INT64_C(0)};
        return result;
    }
    result.is_ok = 1;
    result.value = (RStdTimeSystemTime){seconds, (uint32_t)nanoseconds};
    return result;
}
