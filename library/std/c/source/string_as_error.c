#include "r_std_c.h"

#include <stdint.h>

RStdError r_std_c_string_as_error(RStdCStringError value) {
    return (RStdError){R_STD_ERROR_DOMAIN_C_ABI, (uint32_t)value.kind, INT64_C(0)};
}
