#include "r_std_io.h"

RStdError r_std_io_as_error(RStdIoError value) {
    RStdError result;

    result.domain = R_STD_ERROR_DOMAIN_IO;
    result.code = (uint32_t)value.code;
    result.native_code = value.native_code;
    return result;
}
