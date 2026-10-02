#include "r_std_dict.h"

#include "r_library_dict_internal.h"

RStdDictAllocResult r_std_dict_reserve(RStdDict *target, size_t additional) {
    RStdDictAllocResult result = {0};
    RRuntimeDictStatus status;

    status = r_runtime_dict_reserve(target, additional);
    if (status == R_RUNTIME_DICT_OK) {
        result.status = R_STD_DICT_CALL_SUCCESS;
    } else {
        result.status = r_library_internal_dict_map_allocation_status(status, &result.error);
    }
    return result;
}
