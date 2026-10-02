#include "r_std_time.h"

#include "r_library_clock_internal.h"

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct TestClockState {
    int64_t continuous_status;
    uint64_t ticks;
    uint32_t numerator;
    uint32_t denominator;
    int64_t realtime_status;
    int64_t seconds;
    int64_t nanoseconds;
} TestClockState;

typedef struct ClockThreadState {
    _Bool passed;
} ClockThreadState;

static void fail(const char *message) {
    (void)fprintf(stderr, "library time clock test failed: %s\n", message);
    exit(EXIT_FAILURE);
}

static void require(_Bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

static int compare_instants(RStdTimeInstant left, RStdTimeInstant right) {
    if (left.storage_seconds < right.storage_seconds) {
        return -1;
    }
    if (left.storage_seconds > right.storage_seconds) {
        return 1;
    }
    if (left.storage_nanoseconds < right.storage_nanoseconds) {
        return -1;
    }
    if (left.storage_nanoseconds > right.storage_nanoseconds) {
        return 1;
    }
    return 0;
}

static int64_t
test_read_continuous(void *context, uint64_t *ticks, uint32_t *numerator, uint32_t *denominator) {
    TestClockState *state = context;

    *ticks = state->ticks;
    *numerator = state->numerator;
    *denominator = state->denominator;
    return state->continuous_status;
}

static int64_t test_read_realtime(void *context, int64_t *seconds, int64_t *nanoseconds) {
    TestClockState *state = context;

    *seconds = state->seconds;
    *nanoseconds = state->nanoseconds;
    return state->realtime_status;
}

static _Bool test_status_unavailable(int64_t native_code) {
    return native_code == INT64_C(11);
}

static RLibraryTimeClockHooks test_hooks(TestClockState *state) {
    return (RLibraryTimeClockHooks){
        state,
        test_read_continuous,
        test_read_realtime,
        test_status_unavailable,
        test_status_unavailable,
    };
}

static void require_instant(RStdTimeInstantResult result,
                            int64_t seconds,
                            uint32_t nanoseconds,
                            const char *message) {
    require(result.is_ok && (result.value.storage_seconds == seconds) &&
                (result.value.storage_nanoseconds == nanoseconds),
            message);
}

static void test_mach_conversion(void) {
    RStdTimeInstantResult result;

    result = r_library_internal_time_instant_from_mach(UINT64_C(1), UINT32_C(125), UINT32_C(3));
    require_instant(result, 0, UINT32_C(41), "fractional native tick is quantized downward");
    result = r_library_internal_time_instant_from_mach(UINT64_C(24), UINT32_C(125), UINT32_C(3));
    require_instant(result, 0, UINT32_C(1000), "timebase conversion exact point");
    result = r_library_internal_time_instant_from_mach(
        UINT64_C(24000000000000000), UINT32_C(125), UINT32_C(3));
    require_instant(
        result, INT64_C(1000000000), 0U, "large timebase conversion avoids intermediate overflow");

    result = r_library_internal_time_instant_from_mach(UINT64_MAX, UINT32_MAX, UINT32_C(1));
    require(!result.is_ok && (result.error.code == R_STD_TIME_ERROR_OVERFLOW) &&
                (result.error.native_code == 0),
            "target instant horizon overflow");
    result = r_library_internal_time_instant_from_mach(UINT64_C(1), UINT32_C(1), 0U);
    require(!result.is_ok && (result.error.code == R_STD_TIME_ERROR_OTHER) &&
                (result.error.native_code == 0),
            "malformed native timebase");
}

static void test_clock_failure_mapping(void) {
    TestClockState state = {0};
    RLibraryTimeClockHooks hooks = test_hooks(&state);
    RStdTimeInstantResult instant;
    RStdTimeSystemTimeResult system;

    state.continuous_status = INT64_C(11);
    instant = r_library_internal_time_monotonic_now(&hooks);
    require(!instant.is_ok && (instant.error.code == R_STD_TIME_ERROR_UNAVAILABLE) &&
                (instant.error.native_code == INT64_C(11)),
            "recognized monotonic native failure");
    state.continuous_status = INT64_C(77);
    instant = r_library_internal_time_monotonic_now(&hooks);
    require(!instant.is_ok && (instant.error.code == R_STD_TIME_ERROR_OTHER) &&
                (instant.error.native_code == INT64_C(77)),
            "unclassified monotonic native failure");

    state.realtime_status = INT64_C(11);
    system = r_library_internal_time_system_now(&hooks);
    require(!system.is_ok && (system.error.code == R_STD_TIME_ERROR_UNAVAILABLE) &&
                (system.error.native_code == INT64_C(11)),
            "recognized realtime native failure");
    state.realtime_status = 0;
    state.nanoseconds = INT64_C(1000000000);
    system = r_library_internal_time_system_now(&hooks);
    require(!system.is_ok && (system.error.code == R_STD_TIME_ERROR_OTHER) &&
                (system.error.native_code == 0),
            "malformed successful native realtime value");
}

static void test_instant_arithmetic(void) {
    RStdTimeInstantResult instant;
    RStdTimeDurationTimeResult duration;

    instant = r_std_time_instant_add((RStdTimeInstant){INT64_C(1), UINT32_C(900000000)},
                                     (RStdTimeDuration){0, UINT32_C(200000000)});
    require_instant(instant, INT64_C(2), UINT32_C(100000000), "instant addition carry");
    instant = r_std_time_instant_add((RStdTimeInstant){0, 0U},
                                     (RStdTimeDuration){-INT64_C(1), UINT32_C(999999999)});
    require(!instant.is_ok && (instant.error.code == R_STD_TIME_ERROR_OVERFLOW),
            "instant cannot precede target origin");
    instant = r_std_time_instant_add((RStdTimeInstant){INT64_MAX, UINT32_C(999999999)},
                                     (RStdTimeDuration){0, UINT32_C(1)});
    require(!instant.is_ok && (instant.error.code == R_STD_TIME_ERROR_OVERFLOW),
            "instant target horizon checked");
    instant = r_std_time_instant_add((RStdTimeInstant){0, R_STD_TIME_NANOSECONDS_PER_SECOND},
                                     (RStdTimeDuration){0, 0U});
    require(!instant.is_ok && (instant.error.code == R_STD_TIME_ERROR_INVALID_VALUE),
            "invalid opaque representation rejected at C boundary");

    duration = r_std_time_instant_duration((RStdTimeInstant){INT64_C(5), UINT32_C(100000000)},
                                           (RStdTimeInstant){INT64_C(2), UINT32_C(900000000)});
    require(duration.is_ok && (duration.value.seconds == INT64_C(2)) &&
                (duration.value.nanoseconds == UINT32_C(200000000)),
            "instant duration exact difference");
    duration = r_std_time_instant_duration((RStdTimeInstant){INT64_C(2), 0U},
                                           (RStdTimeInstant){INT64_C(3), 0U});
    require(!duration.is_ok && (duration.error.code == R_STD_TIME_ERROR_INVALID_VALUE),
            "instant operand order checked");
}

static void *clock_thread_main(void *context) {
    ClockThreadState *state = context;
    RStdTimeInstant previous = {0};
    size_t index;

    state->passed = 1;
    for (index = 0U; index < 1000U; ++index) {
        RStdTimeInstantResult reading = r_std_time_monotonic_now();

        if (!reading.is_ok || ((index != 0U) && (compare_instants(reading.value, previous) < 0))) {
            state->passed = 0;
            return NULL;
        }
        previous = reading.value;
    }
    return NULL;
}

static void test_native_clocks_concurrently(void) {
    enum {
        THREAD_COUNT = 4
    };
    pthread_t threads[THREAD_COUNT];
    ClockThreadState states[THREAD_COUNT] = {0};
    RStdTimeSystemTimeResult system;
    size_t index;

    system = r_std_time_system_now();
    require(system.is_ok && (system.value.nanoseconds < R_STD_TIME_NANOSECONDS_PER_SECOND),
            "native system clock normalized");
    for (index = 0U; index < THREAD_COUNT; ++index) {
        require(pthread_create(&threads[index], NULL, clock_thread_main, &states[index]) == 0,
                "create monotonic reader thread");
    }
    for (index = 0U; index < THREAD_COUNT; ++index) {
        require(pthread_join(threads[index], NULL) == 0, "join monotonic reader thread");
        require(states[index].passed, "monotonic readings never regress within a thread");
    }
}

static void test_error_conversion(void) {
    RStdError converted =
        r_std_time_as_error((RStdTimeError){R_STD_TIME_ERROR_OTHER, -INT64_C(987654321)});

    require((converted.domain == R_STD_ERROR_DOMAIN_TIME) &&
                (converted.code == (uint32_t)R_STD_TIME_ERROR_OTHER) &&
                (converted.native_code == -INT64_C(987654321)),
            "time error conversion preserves classification and native status");
}

int main(void) {
    test_mach_conversion();
    test_clock_failure_mapping();
    test_instant_arithmetic();
    test_native_clocks_concurrently();
    test_error_conversion();
    (void)fprintf(stdout, "library_time_clock_tests: ok\n");
    return EXIT_SUCCESS;
}
