#include "r_std_bytes.h"

int32_t r_std_bytes_compare(RStdBytesSlice left, RStdBytesSlice right) {
    size_t common_length = left.length < right.length ? left.length : right.length;
    size_t index;

    for (index = 0U; index < common_length; ++index) {
        if (left.data[index] < right.data[index]) {
            return -INT32_C(1);
        }
        if (left.data[index] > right.data[index]) {
            return INT32_C(1);
        }
    }
    if (left.length < right.length) {
        return -INT32_C(1);
    }
    if (left.length > right.length) {
        return INT32_C(1);
    }
    return INT32_C(0);
}
