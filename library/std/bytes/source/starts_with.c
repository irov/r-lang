#include "r_std_bytes.h"

_Bool r_std_bytes_starts_with(RStdBytesSlice source, RStdBytesSlice prefix) {
    size_t index;

    if (prefix.length > source.length) {
        return 0;
    }
    for (index = 0U; index < prefix.length; ++index) {
        if (source.data[index] != prefix.data[index]) {
            return 0;
        }
    }
    return 1;
}
