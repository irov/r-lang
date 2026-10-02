#include "r_std_string.h"

RStdStringCreateResult r_std_string_create(RRuntimeAllocator *allocator) {
    RStdStringCreateResult result = {0};
    RRuntimeStringStatus status;

    status = r_runtime_string_initialize(&result.value, allocator);
    (void)status;
    result.status = R_STD_STRING_CALL_SUCCESS;
    return result;
}
