#include "r_std_bytes.h"

RStdBytesIndexOption r_std_bytes_find(RStdBytesSlice source, uint8_t value) {
    RStdBytesIndexOption result = {0};
    size_t index;

    for (index = 0U; index < source.length; ++index) {
        if (source.data[index] == value) {
            result.has_value = 1;
            result.value = index;
            return result;
        }
    }
    return result;
}
