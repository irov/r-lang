#include "r_std_c.h"

#include "r_runtime_utf8.h"

#include <stddef.h>
#include <stdint.h>

RStdCValidateUtf8Result r_std_c_validate_utf8(RStdCCharSlice storage) {
    RStdCValidateUtf8Result result = {0};
    size_t length;
    size_t invalid_index = 0U;

    for (length = 0U; length < storage.length; length += 1U) {
        if (((const unsigned char *)storage.data)[length] == UINT8_C(0)) {
            break;
        }
    }
    if (length == storage.length) {
        result.status = R_STD_C_CALL_ERROR;
        result.error.kind = R_STD_C_STRING_ERROR_MISSING_NUL;
        return result;
    }
    if (!r_runtime_utf8_validate((const uint8_t *)storage.data, length, &invalid_index)) {
        result.status = R_STD_C_CALL_ERROR;
        result.error.kind = R_STD_C_STRING_ERROR_INVALID_UTF8;
        result.error.index = invalid_index;
        return result;
    }
    result.status = R_STD_C_CALL_SUCCESS;
    result.value.data = (const uint8_t *)storage.data;
    result.value.length = length;
    return result;
}
