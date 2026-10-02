#include "r_std_array.h"

RStdArrayValueOptionResult
r_std_array_remove(RStdArray *target, size_t index, void *result_storage) {
    RStdArrayValueOptionResult result = {0};

    result.status = R_STD_ARRAY_CALL_SUCCESS;
    result.has_value = r_runtime_array_remove(target, index, result_storage);
    return result;
}
