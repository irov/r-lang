#include "r_std_error.h"

#include <stdint.h>

RStdError r_std_error_from_format(RStdFormatError value) {
    return (RStdError){R_STD_ERROR_DOMAIN_FORMAT, (uint32_t)value.kind, INT64_C(0)};
}
