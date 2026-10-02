#include "r_std_env.h"

#include <stdint.h>

RStdError r_std_env_as_error(RStdEnvError value) {
    return (RStdError){R_STD_ERROR_DOMAIN_ENVIRONMENT, (uint32_t)value.code, value.native_code};
}
