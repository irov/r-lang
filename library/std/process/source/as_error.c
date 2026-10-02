#include "r_std_process.h"

#include <stdint.h>

RStdError r_std_process_as_error(RStdProcessError value) {
    return (RStdError){R_STD_ERROR_DOMAIN_PROCESS, (uint32_t)value.code, value.native_code};
}
