#include "r_std_bytes.h"

#include <string.h>

RStdBytesSizeResult r_std_bytes_copy(RStdBytesMutSlice destination, RStdBytesSlice source) {
    RStdBytesSizeResult result = {0};

    if (destination.length < source.length) {
        result.error = R_STD_BYTES_ERROR_OUT_OF_BOUNDS;
        return result;
    }
    if (source.length != 0U) {
        (void)memmove(destination.data, source.data, source.length);
    }
    result.is_ok = 1;
    result.value = source.length;
    return result;
}
