#include "r_std_error.h"

#include <stdint.h>

RStdError r_std_error_from_async(RStdAsyncStartError value) {
    return (RStdError){R_STD_ERROR_DOMAIN_ASYNC_RUNTIME, (uint32_t)value, INT64_C(0)};
}
