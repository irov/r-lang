#include "r_std_bytes.h"

#include "r_library_bytes_internal.h"

RStdBytesArrayResult r_std_bytes_with_capacity(RRuntimeAllocator *allocator, size_t capacity) {
    const RRuntimeTypeInfo byte_type = {
        sizeof(uint8_t),
        _Alignof(uint8_t),
        NULL,
        NULL,
    };
    RStdBytesArrayResult result = {0};
    RRuntimeArrayStatus status;

    status = r_runtime_array_with_capacity(&result.value, allocator, byte_type, capacity);
    if (status == R_RUNTIME_ARRAY_OK) {
        result.status = R_STD_ALLOC_CALL_SUCCESS;
    } else {
        result.status = r_library_internal_bytes_map_allocation_status(status, &result.error);
    }
    return result;
}
