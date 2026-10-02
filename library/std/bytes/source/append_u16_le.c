#include "r_std_bytes.h"

#include "r_library_bytes_internal.h"

RStdBytesAllocResult r_std_bytes_append_u16_le(RStdBytes *target, uint16_t value) {
    const uint8_t encoded[2] = {
        (uint8_t)value,
        (uint8_t)(value >> 8U),
    };

    return r_library_internal_bytes_append(target, encoded, sizeof(encoded));
}
