#include "r_std_array.h"

#include "r_library_array_internal.h"

size_t r_std_array_capacity(const RStdArray *source) {
    return source->capacity;
}
