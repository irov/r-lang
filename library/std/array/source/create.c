#include "r_std_array.h"

RStdArrayCreateResult r_std_array_create(RRuntimeAllocator *allocator, RRuntimeTypeInfo element) {
    RStdArrayCreateResult result = {0};
    RRuntimeArrayStatus status;

    status = r_runtime_array_with_capacity(&result.value, allocator, element, 0U);
    (void)status;
    result.status = R_STD_ARRAY_CALL_SUCCESS;
    return result;
}
