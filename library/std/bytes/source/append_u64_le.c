#include "r_std_bytes.h"

#include "r_library_bytes_internal.h"

RStdBytesAllocResult r_std_bytes_append_u64_le(RStdBytes *target, uint64_t value) {
    const uint8_t encoded[8] = {
        (uint8_t)value,
        (uint8_t)(value >> 8U),
        (uint8_t)(value >> 16U),
        (uint8_t)(value >> 24U),
        (uint8_t)(value >> 32U),
        (uint8_t)(value >> 40U),
        (uint8_t)(value >> 48U),
        (uint8_t)(value >> 56U),
    };

    return r_library_internal_bytes_append(target, encoded, sizeof(encoded));
}
