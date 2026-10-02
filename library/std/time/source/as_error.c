#include "r_std_time.h"

#include <stdint.h>

RStdError r_std_time_as_error(RStdTimeError value) {
    return (RStdError){R_STD_ERROR_DOMAIN_TIME, (uint32_t)value.code, value.native_code};
}
