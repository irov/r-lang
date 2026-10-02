#include "r_std_error.h"

#include <stdint.h>

RStdError r_std_error_from_boundary(RStdStringBoundaryError value) {
    return (RStdError){
        R_STD_ERROR_DOMAIN_STRING,
        UINT32_C(0x0100) + (uint32_t)value,
        INT64_C(0),
    };
}
