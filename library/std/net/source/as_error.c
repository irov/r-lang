#include "r_std_net.h"

#include <stdint.h>

RStdError r_std_net_as_error(RStdNetError value) {
    return (RStdError){R_STD_ERROR_DOMAIN_NETWORK, (uint32_t)value.code, value.native_code};
}
