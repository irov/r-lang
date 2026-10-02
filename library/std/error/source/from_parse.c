#include "r_std_error.h"

#include <stdint.h>

RStdError r_std_error_from_parse(RStdConvertParseError value) {
    return (RStdError){R_STD_ERROR_DOMAIN_CONVERSION, (uint32_t)value.code, INT64_C(0)};
}
