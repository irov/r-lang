#include "r_std_bytes.h"

#include <stdint.h>
#include <string.h>

RStdBytesSizeResult r_std_bytes_copy_within(RStdBytesMutSlice buffer,
                                            size_t destination,
                                            size_t source,
                                            size_t length) {
    RStdBytesSizeResult result = {0};
    size_t destination_end;
    size_t source_end;

    if (length > (SIZE_MAX - destination)) {
        result.error = R_STD_BYTES_ERROR_RANGE_OVERFLOW;
        return result;
    }
    destination_end = destination + length;
    if (length > (SIZE_MAX - source)) {
        result.error = R_STD_BYTES_ERROR_RANGE_OVERFLOW;
        return result;
    }
    source_end = source + length;
    if (destination_end > buffer.length) {
        result.error = R_STD_BYTES_ERROR_OUT_OF_BOUNDS;
        return result;
    }
    if (source_end > buffer.length) {
        result.error = R_STD_BYTES_ERROR_OUT_OF_BOUNDS;
        return result;
    }
    if (length != 0U) {
        (void)memmove(buffer.data + destination, buffer.data + source, length);
    }
    result.is_ok = 1;
    result.value = length;
    return result;
}
