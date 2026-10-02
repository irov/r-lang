#include "r_std_error.h"

#include <stdint.h>

RStdError r_std_error_from_bytes(RStdBytesError value) {
    return (RStdError){R_STD_ERROR_DOMAIN_BYTES, (uint32_t)value, INT64_C(0)};
}
