#include "r_std_bytes.h"

#include "r_library_bytes_internal.h"

RStdBytesAllocResult r_std_bytes_append(RStdBytes *target, RStdBytesSlice source) {
    return r_library_internal_bytes_append(target, source.data, source.length);
}
