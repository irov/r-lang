#include "r_std_array.h"

#include "r_library_array_internal.h"

#include <string.h>

/*
 * R-LIB-0019 (P4.2): length copies of one Copy value in one allocation. A Copy value is copied
 * bitwise: a one-byte element is one memset; otherwise the staged value is copied once and the
 * filled prefix is doubled by memcpy until it covers the array.
 */
RStdArrayAllocValueResult r_std_array_filled(RRuntimeAllocator *allocator,
                                             RRuntimeTypeInfo element,
                                             size_t length,
                                             const void *value) {
    RStdArrayAllocValueResult result = {0};
    RRuntimeArrayStatus status;
    unsigned char *data;
    size_t filled;
    size_t total;

    status = r_runtime_array_with_capacity(&result.value, allocator, element, length);
    if (status != R_RUNTIME_ARRAY_OK) {
        result.status = r_library_internal_array_map_allocation_status(status, &result.error);
        return result;
    }
    result.status = R_STD_ARRAY_CALL_SUCCESS;
    if ((length != 0U) && (element.size != 0U)) {
        data = result.value.data;
        if (element.size == 1U) {
            (void)memset(data, *(const unsigned char *)value, length);
        } else {
            /* The capacity was allocated, so length * size does not overflow. */
            total = length * element.size;
            (void)memcpy(data, value, element.size);
            filled = element.size;
            while (filled < total) {
                const size_t span = filled <= (total - filled) ? filled : (total - filled);

                (void)memcpy(data + filled, data, span);
                filled += span;
            }
        }
    }
    result.value.length = length;
    return result;
}
