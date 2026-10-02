#include "r_std_time.h"

#include <limits.h>
#include <stddef.h>

static _Bool leap_year(int64_t year) {
    return ((year % INT64_C(4)) == 0) &&
           (((year % INT64_C(100)) != 0) || ((year % INT64_C(400)) == 0));
}

static uint8_t days_in_month(int64_t year, uint8_t month) {
    static const uint8_t days[] = {
        UINT8_C(31),
        UINT8_C(28),
        UINT8_C(31),
        UINT8_C(30),
        UINT8_C(31),
        UINT8_C(30),
        UINT8_C(31),
        UINT8_C(31),
        UINT8_C(30),
        UINT8_C(31),
        UINT8_C(30),
        UINT8_C(31),
    };

    if (month == UINT8_C(2) && leap_year(year)) {
        return UINT8_C(29);
    }
    return days[(size_t)month - 1U];
}

RStdTimeSystemTimeResult r_std_time_from_utc(RStdTimeUtcDateTime value) {
    RStdTimeSystemTimeResult result = {0};
    int64_t year = value.year;
    int64_t month = value.month;
    int64_t era;
    int64_t year_of_era;
    int64_t month_prime;
    int64_t day_of_year;
    int64_t day_of_era;
    int64_t days;
    int64_t seconds;

    if ((value.month < UINT8_C(1)) || (value.month > UINT8_C(12)) || (value.day < UINT8_C(1)) ||
        (value.day > days_in_month(year, value.month)) || (value.hour > UINT8_C(23)) ||
        (value.minute > UINT8_C(59)) || (value.second > UINT8_C(59)) ||
        (value.nanosecond >= R_STD_TIME_NANOSECONDS_PER_SECOND)) {
        result.error.code = R_STD_TIME_ERROR_INVALID_VALUE;
        return result;
    }
    year -= month <= INT64_C(2) ? INT64_C(1) : INT64_C(0);
    era = (year >= 0 ? year : year - INT64_C(399)) / INT64_C(400);
    year_of_era = year - era * INT64_C(400);
    month_prime = month + (month > INT64_C(2) ? -INT64_C(3) : INT64_C(9));
    day_of_year =
        (INT64_C(153) * month_prime + INT64_C(2)) / INT64_C(5) + (int64_t)value.day - INT64_C(1);
    day_of_era = year_of_era * INT64_C(365) + year_of_era / INT64_C(4) -
                 year_of_era / INT64_C(100) + day_of_year;
    days = era * INT64_C(146097) + day_of_era - INT64_C(719468);
    if ((days > (INT64_MAX / INT64_C(86400))) || (days < (INT64_MIN / INT64_C(86400)))) {
        result.error.code = R_STD_TIME_ERROR_OVERFLOW;
        return result;
    }
    seconds = days * INT64_C(86400);
    if (seconds > (INT64_MAX - (int64_t)value.hour * INT64_C(3600) -
                   (int64_t)value.minute * INT64_C(60) - (int64_t)value.second)) {
        result.error.code = R_STD_TIME_ERROR_OVERFLOW;
        return result;
    }
    seconds += (int64_t)value.hour * INT64_C(3600);
    seconds += (int64_t)value.minute * INT64_C(60);
    seconds += (int64_t)value.second;
    result.is_ok = 1;
    result.value.unix_seconds = seconds;
    result.value.nanoseconds = value.nanosecond;
    return result;
}
