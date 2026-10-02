#include "r_std_time.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static void fail(const char *message) {
    (void)fprintf(stderr, "library duration test failed: %s\n", message);
    exit(EXIT_FAILURE);
}

static void require(_Bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

static void require_duration(RStdTimeDurationResult result,
                             int64_t seconds,
                             uint32_t nanoseconds,
                             const char *message) {
    require(result.is_ok, message);
    require((result.value.seconds == seconds) && (result.value.nanoseconds == nanoseconds),
            message);
}

static void
require_error(RStdTimeDurationResult result, RStdTimeDurationError error, const char *message) {
    require(!result.is_ok && (result.error == error), message);
}

static void test_construction_and_access(void) {
    RStdTimeDuration value = r_std_time_duration_from_seconds(-INT64_C(9));
    RStdTimeDurationResult parts;

    require(r_std_time_duration_seconds(value) == -INT64_C(9), "seconds construction/access");
    require(r_std_time_duration_nanoseconds(value) == 0U, "seconds construction zero nanos");
    parts = r_std_time_duration_from_parts(INT64_MIN, UINT32_C(999999999));
    require_duration(parts, INT64_MIN, UINT32_C(999999999), "valid boundary parts");
    parts = r_std_time_duration_from_parts(0, R_STD_TIME_NANOSECONDS_PER_SECOND);
    require_error(parts, R_STD_TIME_DURATION_ERROR_INVALID_NANOSECONDS, "invalid nanoseconds");
}

static void test_add_and_subtract(void) {
    RStdTimeDurationResult result;

    result = r_std_time_duration_add((RStdTimeDuration){INT64_C(2), UINT32_C(900000000)},
                                     (RStdTimeDuration){INT64_C(3), UINT32_C(200000000)});
    require_duration(result, INT64_C(6), UINT32_C(100000000), "add normalizes carry");

    result = r_std_time_duration_add((RStdTimeDuration){INT64_MIN, UINT32_C(900000000)},
                                     (RStdTimeDuration){-INT64_C(1), UINT32_C(200000000)});
    require_duration(result,
                     INT64_MIN,
                     UINT32_C(100000000),
                     "add boundary cancellation without false underflow");

    result = r_std_time_duration_add((RStdTimeDuration){INT64_MAX, UINT32_C(900000000)},
                                     (RStdTimeDuration){0, UINT32_C(200000000)});
    require_error(result, R_STD_TIME_DURATION_ERROR_OVERFLOW, "positive add overflow");
    result = r_std_time_duration_add((RStdTimeDuration){INT64_MIN, UINT32_C(100000000)},
                                     (RStdTimeDuration){-INT64_C(1), UINT32_C(800000000)});
    require_error(result, R_STD_TIME_DURATION_ERROR_OVERFLOW, "negative add overflow");

    result = r_std_time_duration_sub((RStdTimeDuration){0, 0U},
                                     (RStdTimeDuration){INT64_MIN, UINT32_C(1)});
    require_duration(
        result, INT64_MAX, UINT32_C(999999999), "subtract minimum plus fraction boundary");
    result = r_std_time_duration_sub((RStdTimeDuration){0, 0U}, (RStdTimeDuration){INT64_MIN, 0U});
    require_error(result, R_STD_TIME_DURATION_ERROR_OVERFLOW, "negating minimum overflows");
    result = r_std_time_duration_sub((RStdTimeDuration){-INT64_C(1), UINT32_C(500000000)},
                                     (RStdTimeDuration){-INT64_C(2), UINT32_C(750000000)});
    require_duration(result, 0, UINT32_C(750000000), "subtract negatives");
}

static void test_multiply(void) {
    RStdTimeDurationResult result;

    result = r_std_time_duration_multiply((RStdTimeDuration){-INT64_C(1), UINT32_C(999999999)},
                                          INT64_MIN);
    require_duration(
        result, INT64_C(9223372036), UINT32_C(854775808), "negative nanosecond times minimum");
    result = r_std_time_duration_multiply((RStdTimeDuration){0, UINT32_C(1)}, INT64_MIN);
    require_duration(
        result, -INT64_C(9223372037), UINT32_C(145224192), "positive nanosecond times minimum");
    result = r_std_time_duration_multiply((RStdTimeDuration){INT64_C(1), 0U}, INT64_MIN);
    require_duration(result, INT64_MIN, 0U, "one second times minimum");
    result = r_std_time_duration_multiply((RStdTimeDuration){INT64_MIN, 0U}, -INT64_C(1));
    require_error(
        result, R_STD_TIME_DURATION_ERROR_OVERFLOW, "minimum times negative one overflows");
    result = r_std_time_duration_multiply((RStdTimeDuration){INT64_MAX, UINT32_C(999999999)},
                                          INT64_C(2));
    require_error(result, R_STD_TIME_DURATION_ERROR_OVERFLOW, "positive multiply overflow");
    result = r_std_time_duration_multiply((RStdTimeDuration){INT64_MIN, UINT32_C(999999999)},
                                          INT64_C(0));
    require_duration(result, 0, 0U, "multiply by zero canonicalizes");
}

static void test_compare(void) {
    require(r_std_time_duration_compare((RStdTimeDuration){-INT64_C(1), UINT32_C(900000000)},
                                        (RStdTimeDuration){0, 0U}) == -INT32_C(1),
            "negative compare");
    require(r_std_time_duration_compare((RStdTimeDuration){INT64_C(4), UINT32_C(1)},
                                        (RStdTimeDuration){INT64_C(4), 0U}) == INT32_C(1),
            "fraction compare");
    require(r_std_time_duration_compare((RStdTimeDuration){INT64_MIN, 0U},
                                        (RStdTimeDuration){INT64_MIN, 0U}) == 0,
            "equal compare");
}

static RStdTimeDuration duration_from_small_nanoseconds(int64_t total_nanoseconds) {
    const int64_t base = INT64_C(1000000000);
    RStdTimeDuration result;
    int64_t remainder;

    result.seconds = total_nanoseconds / base;
    remainder = total_nanoseconds % base;
    if (remainder < 0) {
        result.seconds -= INT64_C(1);
        remainder += base;
    }
    result.nanoseconds = (uint32_t)remainder;
    return result;
}

static void require_same_duration(RStdTimeDurationResult actual,
                                  RStdTimeDuration expected,
                                  const char *message) {
    require(actual.is_ok && (actual.value.seconds == expected.seconds) &&
                (actual.value.nanoseconds == expected.nanoseconds),
            message);
}

static void test_small_exhaustive_arithmetic(void) {
    static const uint32_t fractions[] = {
        0U,
        UINT32_C(1),
        UINT32_C(250000000),
        UINT32_C(500000000),
        UINT32_C(999999999),
    };
    int64_t left_seconds;
    int64_t right_seconds;
    size_t left_fraction;
    size_t right_fraction;
    int64_t factor;

    for (left_seconds = -INT64_C(20); left_seconds <= INT64_C(20); ++left_seconds) {
        for (left_fraction = 0U; left_fraction < (sizeof(fractions) / sizeof(fractions[0]));
             ++left_fraction) {
            const RStdTimeDuration left = {left_seconds, fractions[left_fraction]};
            const int64_t left_total =
                left_seconds * INT64_C(1000000000) + (int64_t)fractions[left_fraction];

            for (right_seconds = -INT64_C(20); right_seconds <= INT64_C(20); ++right_seconds) {
                for (right_fraction = 0U;
                     right_fraction < (sizeof(fractions) / sizeof(fractions[0]));
                     ++right_fraction) {
                    const RStdTimeDuration right = {
                        right_seconds,
                        fractions[right_fraction],
                    };
                    const int64_t right_total =
                        right_seconds * INT64_C(1000000000) + (int64_t)fractions[right_fraction];

                    require_same_duration(r_std_time_duration_add(left, right),
                                          duration_from_small_nanoseconds(left_total + right_total),
                                          "small exhaustive add");
                    require_same_duration(r_std_time_duration_sub(left, right),
                                          duration_from_small_nanoseconds(left_total - right_total),
                                          "small exhaustive subtract");
                }
            }
            for (factor = -INT64_C(20); factor <= INT64_C(20); ++factor) {
                require_same_duration(r_std_time_duration_multiply(left, factor),
                                      duration_from_small_nanoseconds(left_total * factor),
                                      "small exhaustive multiply");
            }
        }
    }
}

static void require_system_time(RStdTimeSystemTimeResult result,
                                int64_t seconds,
                                uint32_t nanoseconds,
                                const char *message) {
    require(result.is_ok && (result.value.unix_seconds == seconds) &&
                (result.value.nanoseconds == nanoseconds),
            message);
}

static void
require_time_error(RStdTimeErrorCode actual, RStdTimeErrorCode expected, const char *message) {
    require(actual == expected, message);
}

static void test_system_add(void) {
    RStdTimeSystemTimeResult result;

    result = r_std_time_system_add((RStdTimeSystemTime){INT64_C(4), UINT32_C(900000000)},
                                   (RStdTimeDuration){-INT64_C(2), UINT32_C(200000000)});
    require_system_time(result, INT64_C(3), UINT32_C(100000000), "system add");
    result = r_std_time_system_add((RStdTimeSystemTime){INT64_MAX, UINT32_C(900000000)},
                                   (RStdTimeDuration){0, UINT32_C(200000000)});
    require(!result.is_ok, "system add overflow outcome");
    require_time_error(
        result.error.code, R_STD_TIME_ERROR_OVERFLOW, "system add overflow classification");
    result = r_std_time_system_add((RStdTimeSystemTime){0, R_STD_TIME_NANOSECONDS_PER_SECOND},
                                   (RStdTimeDuration){0, 0U});
    require(!result.is_ok, "system add invalid outcome");
    require_time_error(
        result.error.code, R_STD_TIME_ERROR_INVALID_VALUE, "system add invalid classification");
}

static void require_utc(RStdTimeUtcDateTimeResult result,
                        int32_t year,
                        uint8_t month,
                        uint8_t day,
                        uint8_t hour,
                        uint8_t minute,
                        uint8_t second,
                        uint32_t nanosecond,
                        const char *message) {
    require(result.is_ok && (result.value.year == year) && (result.value.month == month) &&
                (result.value.day == day) && (result.value.hour == hour) &&
                (result.value.minute == minute) && (result.value.second == second) &&
                (result.value.nanosecond == nanosecond),
            message);
}

static void test_utc_conversion(void) {
    RStdTimeSystemTimeResult system;
    RStdTimeUtcDateTimeResult utc;
    RStdTimeUtcDateTime invalid;
    static const int32_t round_trip_years[] = {
        INT32_MIN,
        -INT32_C(400),
        -INT32_C(1),
        0,
        INT32_C(1970),
        INT32_C(2000),
        INT32_MAX,
    };
    size_t index;

    require_utc(r_std_time_to_utc((RStdTimeSystemTime){0, UINT32_C(7)}),
                INT32_C(1970),
                UINT8_C(1),
                UINT8_C(1),
                0U,
                0U,
                0U,
                UINT32_C(7),
                "Unix epoch UTC");
    require_utc(r_std_time_to_utc((RStdTimeSystemTime){-INT64_C(1), 0U}),
                INT32_C(1969),
                UINT8_C(12),
                UINT8_C(31),
                UINT8_C(23),
                UINT8_C(59),
                UINT8_C(59),
                0U,
                "negative Unix second UTC");
    system = r_std_time_from_utc(
        (RStdTimeUtcDateTime){INT32_C(2000), UINT8_C(2), UINT8_C(29), 0U, 0U, 0U, UINT32_C(9)});
    require_system_time(system, INT64_C(951782400), UINT32_C(9), "leap-day Unix value");

    for (index = 0U; index < (sizeof(round_trip_years) / sizeof(round_trip_years[0])); ++index) {
        const RStdTimeUtcDateTime source = {
            round_trip_years[index],
            UINT8_C(3),
            UINT8_C(1),
            UINT8_C(12),
            UINT8_C(34),
            UINT8_C(56),
            UINT32_C(987654321),
        };

        system = r_std_time_from_utc(source);
        require(system.is_ok, "extreme UTC year is representable as system time");
        utc = r_std_time_to_utc(system.value);
        require_utc(utc,
                    source.year,
                    source.month,
                    source.day,
                    source.hour,
                    source.minute,
                    source.second,
                    source.nanosecond,
                    "UTC extreme-year round trip");
    }

    invalid = (RStdTimeUtcDateTime){INT32_C(1900), UINT8_C(2), UINT8_C(29), 0U, 0U, 0U, 0U};
    system = r_std_time_from_utc(invalid);
    require(!system.is_ok && (system.error.code == R_STD_TIME_ERROR_INVALID_VALUE),
            "non-leap February 29 rejected");
    invalid = (RStdTimeUtcDateTime){INT32_C(2000), UINT8_C(1), UINT8_C(1), 0U, 0U, UINT8_C(60), 0U};
    system = r_std_time_from_utc(invalid);
    require(!system.is_ok && (system.error.code == R_STD_TIME_ERROR_INVALID_VALUE),
            "leap second rejected");
    invalid.month = 0U;
    system = r_std_time_from_utc(invalid);
    require(!system.is_ok && (system.error.code == R_STD_TIME_ERROR_INVALID_VALUE),
            "zero month rejected before indexing");
    utc = r_std_time_to_utc((RStdTimeSystemTime){0, R_STD_TIME_NANOSECONDS_PER_SECOND});
    require(!utc.is_ok && (utc.error.code == R_STD_TIME_ERROR_INVALID_VALUE),
            "invalid system nanoseconds rejected");
    utc = r_std_time_to_utc((RStdTimeSystemTime){INT64_MAX, 0U});
    require(!utc.is_ok && (utc.error.code == R_STD_TIME_ERROR_OVERFLOW),
            "UTC year overflow classified");
}

int main(void) {
    test_construction_and_access();
    test_add_and_subtract();
    test_multiply();
    test_compare();
    test_small_exhaustive_arithmetic();
    test_system_add();
    test_utc_conversion();
    (void)fprintf(stdout, "library_time_duration_tests: ok\n");
    return EXIT_SUCCESS;
}
