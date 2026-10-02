#include "r_std_bits.h"

static uint64_t r_std_bits_low_mask(uint8_t width) {
    if (width == 64U) {
        return UINT64_MAX;
    }
    return (UINT64_C(1) << width) - UINT64_C(1);
}

RStdBitsReadResult
r_std_bits_read(RStdBitsByteView input, RStdBitsLsbReader *reader, uint8_t width) {
    RStdBitsReadResult result = {0};
    RStdBitsLsbReader next = *reader;
    uint8_t remaining = width;
    uint8_t destination_offset = 0U;

    if ((width > 64U) || (next.bit_count > 64U)) {
        result.error.code = R_STD_BITS_READ_ERROR_INVALID_WIDTH;
        result.error.byte_index = reader->byte_index;
        return result;
    }

    if (remaining > next.bit_count) {
        const uint8_t required_after_hold = (uint8_t)(remaining - next.bit_count);
        const size_t required_bytes = (size_t)((required_after_hold + 7U) / 8U);

        if (next.byte_index > input.length) {
            result.error.code = R_STD_BITS_READ_ERROR_UNEXPECTED_END;
            result.error.byte_index = next.byte_index;
            return result;
        }
        if (required_bytes > input.length - next.byte_index) {
            result.error.code = R_STD_BITS_READ_ERROR_UNEXPECTED_END;
            result.error.byte_index = input.length;
            return result;
        }
    }

    if (next.bit_count != 0U) {
        const uint8_t take = remaining < next.bit_count ? remaining : next.bit_count;
        const uint64_t mask = r_std_bits_low_mask(take);

        result.value = next.hold & mask;
        if (take == 64U) {
            next.hold = UINT64_C(0);
        } else {
            next.hold >>= take;
        }
        next.bit_count = (uint8_t)(next.bit_count - take);
        remaining = (uint8_t)(remaining - take);
        destination_offset = take;
    }

    while (remaining != 0U) {
        const uint8_t byte = input.data[next.byte_index];
        const uint8_t take = remaining < 8U ? remaining : 8U;
        const uint64_t mask = r_std_bits_low_mask(take);

        result.value |= ((uint64_t)byte & mask) << destination_offset;
        next.byte_index += 1U;
        remaining = (uint8_t)(remaining - take);
        destination_offset = (uint8_t)(destination_offset + take);

        if (take != 8U) {
            next.hold = (uint64_t)(byte >> take);
            next.bit_count = (uint8_t)(8U - take);
        }
    }

    result.is_ok = 1;
    *reader = next;
    return result;
}
