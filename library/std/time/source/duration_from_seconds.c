#include "r_std_time.h"

RStdTimeDuration r_std_time_duration_from_seconds(int64_t seconds) {
    RStdTimeDuration result = {seconds, 0U};

    return result;
}
