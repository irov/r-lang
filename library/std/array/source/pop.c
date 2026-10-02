#include "r_std_array.h"

RStdArrayValueOptionResult r_std_array_pop(RStdArray *target, void *result_storage) {
    RStdArrayValueOptionResult result = {0};

    result.status = R_STD_ARRAY_CALL_SUCCESS;
    result.has_value = r_runtime_array_pop(target, result_storage);
    return result;
}
