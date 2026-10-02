#include "r_std_list.h"

#include "r_library_list_internal.h"

RStdListInsertResult
r_std_list_insert_before(RStdList *target, const void *position, void *staged_value) {
    RStdListInsertResult result = {0};
    RRuntimeListStatus status;

    status = r_runtime_list_insert_before(target, position, staged_value, &result.value);
    if (status == R_RUNTIME_LIST_OK) {
        result.status = R_STD_LIST_CALL_SUCCESS;
    } else {
        result.status = r_library_internal_list_map_allocation_status(status, &result.reason);
    }
    return result;
}
