#include "r_std_dict.h"

#include "r_library_dict_internal.h"

RStdDictInsertResult
r_std_dict_insert(RStdDict *target, void *staged_key, void *staged_value, void *replaced_storage) {
    RStdDictInsertResult result = {0};
    RRuntimeDictStatus status;

    status = r_runtime_dict_insert(
        target, staged_key, staged_value, replaced_storage, &result.did_replace);
    if (status == R_RUNTIME_DICT_OK) {
        result.status = R_STD_DICT_CALL_SUCCESS;
    } else {
        result.status = r_library_internal_dict_map_allocation_status(status, &result.reason);
    }
    return result;
}
