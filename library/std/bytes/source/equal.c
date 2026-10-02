#include "r_std_bytes.h"

_Bool r_std_bytes_equal(RStdBytesSlice left, RStdBytesSlice right) {
    size_t index;

    if (left.length != right.length) {
        return 0;
    }
    for (index = 0U; index < left.length; ++index) {
        if (left.data[index] != right.data[index]) {
            return 0;
        }
    }
    return 1;
}
