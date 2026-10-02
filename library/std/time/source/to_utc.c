#include "r_std_time.h"

#include <limits.h>

RStdTimeUtcDateTimeResult r_std_time_to_utc(RStdTimeSystemTime value) {
    const int64_t seconds_per_day = INT64_C(86400);
    RStdTimeUtcDateTimeResult result = {0};
    int64_t days;
    int64_t seconds_in_day;
    int64_t shifted_days;
    int64_t era;
    int64_t day_of_era;
    int64_t year_of_era;
    int64_t year;
    int64_t day_of_year;
    int64_t month_prime;
    int64_t month;
    int64_t day;

    if (value.nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        result.error.code = R_STD_TIME_ERROR_INVALID_VALUE;
        return result;
    }
    days = value.unix_seconds / seconds_per_day;
    seconds_in_day = value.unix_seconds % seconds_per_day;
    if (seconds_in_day < 0) {
        days -= INT64_C(1);
        seconds_in_day += seconds_per_day;
    }
    shifted_days = days + INT64_C(719468);
    era = (shifted_days >= 0 ? shifted_days : shifted_days - INT64_C(146096)) / INT64_C(146097);
    day_of_era = shifted_days - era * INT64_C(146097);
    year_of_era = (day_of_era - day_of_era / INT64_C(1460) + day_of_era / INT64_C(36524) -
                   day_of_era / INT64_C(146096)) /
                  INT64_C(365);
    year = year_of_era + era * INT64_C(400);
    day_of_year = day_of_era - (INT64_C(365) * year_of_era + year_of_era / INT64_C(4) -
                                year_of_era / INT64_C(100));
    month_prime = (INT64_C(5) * day_of_year + INT64_C(2)) / INT64_C(153);
    day = day_of_year - (INT64_C(153) * month_prime + INT64_C(2)) / INT64_C(5) + INT64_C(1);
    month = month_prime + (month_prime < INT64_C(10) ? INT64_C(3) : -INT64_C(9));
    year += month <= INT64_C(2) ? INT64_C(1) : INT64_C(0);
    if ((year < INT32_MIN) || (year > INT32_MAX)) {
        result.error.code = R_STD_TIME_ERROR_OVERFLOW;
        return result;
    }
    result.is_ok = 1;
    result.value.year = (int32_t)year;
    result.value.month = (uint8_t)month;
    result.value.day = (uint8_t)day;
    result.value.hour = (uint8_t)(seconds_in_day / INT64_C(3600));
    seconds_in_day %= INT64_C(3600);
    result.value.minute = (uint8_t)(seconds_in_day / INT64_C(60));
    result.value.second = (uint8_t)(seconds_in_day % INT64_C(60));
    result.value.nanosecond = value.nanoseconds;
    return result;
}
