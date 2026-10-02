#include "r_core.h"

#include "r_runtime_utf8.h"

RCoreValidateUtf8Result r_core_validate_utf8(RCoreByteSlice source) {
    RCoreValidateUtf8Result result = {0};
    size_t invalid_index = 0U;

    if (!r_runtime_utf8_validate(source.data, source.length, &invalid_index)) {
        result.error.index = invalid_index;
        return result;
    }
    result.is_ok = 1;
    result.value.data = source.data;
    result.value.length = source.length;
    return result;
}
