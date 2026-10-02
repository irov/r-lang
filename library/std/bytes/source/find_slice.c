#include "r_std_bytes.h"

RStdBytesIndexOption r_std_bytes_find_slice(RStdBytesSlice source, RStdBytesSlice pattern) {
    RStdBytesIndexOption result = {0};
    size_t last_start;
    size_t offset;

    if (pattern.length == 0U) {
        result.has_value = 1;
        return result;
    }
    if (pattern.length > source.length) {
        return result;
    }
    last_start = source.length - pattern.length;
    for (offset = 0U; offset <= last_start; ++offset) {
        size_t pattern_index;
        _Bool matches = 1;

        for (pattern_index = 0U; pattern_index < pattern.length; ++pattern_index) {
            if (source.data[offset + pattern_index] != pattern.data[pattern_index]) {
                matches = 0;
                break;
            }
        }
        if (matches) {
            result.has_value = 1;
            result.value = offset;
            return result;
        }
    }
    return result;
}
