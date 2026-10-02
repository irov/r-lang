#include "r_std_array.h"

RStdArrayConstPointerOption r_std_array_get(const RStdArray *source, size_t index) {
    RStdArrayConstPointerOption result = {0};

    result.status = R_STD_ARRAY_CALL_SUCCESS;
    result.value = r_runtime_array_get(source, index);
    result.has_value = result.value != NULL;
    return result;
}
