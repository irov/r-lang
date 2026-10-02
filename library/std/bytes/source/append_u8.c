#include "r_std_bytes.h"

#include "r_library_bytes_internal.h"

RStdBytesAllocResult r_std_bytes_append_u8(RStdBytes *target, uint8_t value) {
    return r_library_internal_bytes_append(target, &value, sizeof(value));
}
