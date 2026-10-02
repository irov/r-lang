#include "r_std_array.h"

RStdArrayMutPointerOption r_std_array_get_mut(RStdArray *source, size_t index) {
    RStdArrayMutPointerOption result = {0};

    result.status = R_STD_ARRAY_CALL_SUCCESS;
    result.value = r_runtime_array_get_mut(source, index);
    result.has_value = result.value != NULL;
    return result;
}
