#include "r_std_fs.h"

#include <stdint.h>

RStdError r_std_fs_as_error(RStdFsError value) {
    return (RStdError){R_STD_ERROR_DOMAIN_FILESYSTEM, (uint32_t)value.code, value.native_code};
}
