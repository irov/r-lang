#include "r_std_dict.h"

#include "r_library_dict_internal.h"

RStdDictAllocValueResult r_std_dict_with_capacity(RRuntimeAllocator *allocator,
                                                  RStdDictKeyInfo key,
                                                  RRuntimeTypeInfo value,
                                                  uint64_t seed,
                                                  size_t capacity) {
    RStdDictAllocValueResult result = {0};
    RRuntimeDictStatus status;

    status = r_runtime_dict_with_capacity(&result.value, allocator, key, value, seed, capacity);
    if (status == R_RUNTIME_DICT_OK) {
        result.status = R_STD_DICT_CALL_SUCCESS;
    } else {
        result.status = r_library_internal_dict_map_allocation_status(status, &result.error);
    }
    return result;
}
