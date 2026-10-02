#include "r_std_math.h"

RStdError r_std_math_as_error(RStdMathError value) {
    return (RStdError){R_STD_ERROR_DOMAIN_MATH, (uint32_t)value.code, value.native_code};
}
