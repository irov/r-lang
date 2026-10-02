#include "r_std_list.h"

RStdListCreateResult r_std_list_create(RRuntimeAllocator *allocator, RRuntimeTypeInfo element) {
    RStdListCreateResult result = {0};
    RRuntimeListStatus status;

    status = r_runtime_list_initialize(&result.value, allocator, element);
    (void)status;
    result.status = R_STD_LIST_CALL_SUCCESS;
    return result;
}
