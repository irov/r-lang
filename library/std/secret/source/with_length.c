#include "r_std_secret.h"

RStdSecretBufferResult r_std_secret_with_length(RRuntimeAllocator *allocator, size_t length) {
    RStdSecretBufferResult result = {0};
    RStdAllocBytesResult allocated = r_std_alloc_bytes(allocator, length, UINT8_C(0));

    result.status = allocated.status;
    result.error = allocated.error;
    result.value = allocated.value;
    return result;
}
