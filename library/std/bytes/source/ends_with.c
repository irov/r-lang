#include "r_std_bytes.h"

_Bool r_std_bytes_ends_with(RStdBytesSlice source, RStdBytesSlice suffix) {
    size_t offset;
    size_t index;

    if (suffix.length > source.length) {
        return 0;
    }
    offset = source.length - suffix.length;
    for (index = 0U; index < suffix.length; ++index) {
        if (source.data[offset + index] != suffix.data[index]) {
            return 0;
        }
    }
    return 1;
}
