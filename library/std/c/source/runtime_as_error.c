#include "r_std_c.h"

#include <stdint.h>

RStdError r_std_c_runtime_as_error(RStdCRuntimeError value) {
    return (RStdError){
        R_STD_ERROR_DOMAIN_C_ABI,
        UINT32_C(0x0100) + (uint32_t)value,
        INT64_C(0),
    };
}
