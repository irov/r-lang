#ifndef R_STD_TIME_H
#define R_STD_TIME_H

#include "r_std_async.h"
#include "r_std_error_types.h"

#include <stdint.h>

#define R_STD_TIME_NANOSECONDS_PER_SECOND UINT32_C(1000000000)

typedef struct RStdTimeDuration {
    int64_t seconds;
    uint32_t nanoseconds;
} RStdTimeDuration;

/* Canonical C representation of o(std.time::duration). */
typedef struct RStdTimeDurationOption {
    uint32_t r_tag;
    union {
        RStdTimeDuration r_some;
    } r_payload;
} RStdTimeDurationOption;

typedef enum RStdTimeDurationError {
    R_STD_TIME_DURATION_ERROR_INVALID_NANOSECONDS = 0,
    R_STD_TIME_DURATION_ERROR_OVERFLOW = 1
} RStdTimeDurationError;

typedef struct RStdTimeDurationResult {
    _Bool is_ok;
    RStdTimeDuration value;
    RStdTimeDurationError error;
} RStdTimeDurationResult;

typedef struct RStdTimeSystemTime {
    int64_t unix_seconds;
    uint32_t nanoseconds;
} RStdTimeSystemTime;

/* Canonical C representation of o(std.time::system_time). */
typedef struct RStdTimeSystemTimeOption {
    uint32_t r_tag;
    union {
        RStdTimeSystemTime r_some;
    } r_payload;
} RStdTimeSystemTimeOption;

/* Opaque to R source. The fields are the private C ABI representation. */
typedef struct RStdTimeInstant {
    int64_t storage_seconds;
    uint32_t storage_nanoseconds;
} RStdTimeInstant;

typedef struct RStdTimeUtcDateTime {
    int32_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint32_t nanosecond;
} RStdTimeUtcDateTime;

typedef enum RStdTimeErrorCode {
    R_STD_TIME_ERROR_INVALID_VALUE = 0,
    R_STD_TIME_ERROR_OVERFLOW = 1,
    R_STD_TIME_ERROR_UNAVAILABLE = 2,
    R_STD_TIME_ERROR_CANCELLED = 3,
    R_STD_TIME_ERROR_OTHER = 4
} RStdTimeErrorCode;

typedef struct RStdTimeError {
    RStdTimeErrorCode code;
    int64_t native_code;
} RStdTimeError;

typedef struct RStdTimeSystemTimeResult {
    _Bool is_ok;
    RStdTimeSystemTime value;
    RStdTimeError error;
} RStdTimeSystemTimeResult;

typedef struct RStdTimeInstantResult {
    _Bool is_ok;
    RStdTimeInstant value;
    RStdTimeError error;
} RStdTimeInstantResult;

typedef struct RStdTimeDurationTimeResult {
    _Bool is_ok;
    RStdTimeDuration value;
    RStdTimeError error;
} RStdTimeDurationTimeResult;

/* Canonical hidden checked carrier: r_tag is 0 for success and 1 for time_error. */
typedef struct RStdTimeTaskResult {
    uint32_t r_tag;
    union {
        RStdTimeError r_error_00000001;
    } r_payload;
} RStdTimeTaskResult;

typedef struct RStdTimeSleepStartResult {
    _Bool is_ok;
    RRuntimeTask *task;
    RStdAsyncStartError error;
} RStdTimeSleepStartResult;

typedef struct RStdTimeUtcDateTimeResult {
    _Bool is_ok;
    RStdTimeUtcDateTime value;
    RStdTimeError error;
} RStdTimeUtcDateTimeResult;

/* Pure, non-allocating, non-panicking R-SLIB-TIME-0001 operations. */
RStdTimeDuration r_std_time_duration_from_seconds(int64_t seconds);
RStdTimeDurationResult r_std_time_duration_from_parts(int64_t seconds, uint32_t nanoseconds);
int64_t r_std_time_duration_seconds(RStdTimeDuration value);
uint32_t r_std_time_duration_nanoseconds(RStdTimeDuration value);
RStdTimeDurationResult r_std_time_duration_add(RStdTimeDuration left, RStdTimeDuration right);
RStdTimeDurationResult r_std_time_duration_sub(RStdTimeDuration left, RStdTimeDuration right);
RStdTimeDurationResult r_std_time_duration_multiply(RStdTimeDuration value, int64_t factor);
int32_t r_std_time_duration_compare(RStdTimeDuration left, RStdTimeDuration right);

/* Pure, non-allocating, non-panicking R-SLIB-TIME-0002/0003 operations. */
RStdTimeSystemTimeResult r_std_time_system_add(RStdTimeSystemTime base, RStdTimeDuration delta);
RStdTimeInstantResult r_std_time_monotonic_now(void);
RStdTimeSystemTimeResult r_std_time_system_now(void);
RStdTimeInstantResult r_std_time_instant_add(RStdTimeInstant base, RStdTimeDuration delta);
RStdTimeDurationTimeResult r_std_time_instant_duration(RStdTimeInstant later,
                                                       RStdTimeInstant earlier);
RStdTimeUtcDateTimeResult r_std_time_to_utc(RStdTimeSystemTime value);
RStdTimeSystemTimeResult r_std_time_from_utc(RStdTimeUtcDateTime value);

/* Eager native tasks; start failure does not consume or modify the Copy argument. */
RStdTimeSleepStartResult r_std_time_sleep_for(RStdTimeDuration duration);
RStdTimeSleepStartResult r_std_time_sleep_until(RStdTimeInstant when);

/* Exact, allocation-free conversion preserving the native status. */
RStdError r_std_time_as_error(RStdTimeError value);

#endif
