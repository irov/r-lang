#include "r_std_dict.h"

RStdDictCreateResult r_std_dict_create(RRuntimeAllocator *allocator,
                                       RStdDictKeyInfo key,
                                       RRuntimeTypeInfo value,
                                       uint64_t seed) {
    RStdDictCreateResult result = {0};
    RRuntimeDictStatus status;

    status = r_runtime_dict_initialize(&result.value, allocator, key, value, seed);
    (void)status;
    result.status = R_STD_DICT_CALL_SUCCESS;
    return result;
}
