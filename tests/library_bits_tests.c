#include "r_std_bits.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "check failed at %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static int r_test_zero_and_byte_reads(void) {
    const uint8_t input[] = {UINT8_C(0xca), UINT8_C(0x74), UINT8_C(0xa5)};
    RStdBitsLsbReader reader = {0};
    RStdBitsReadResult result;

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 0U);
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == UINT64_C(0));
    R_TEST_CHECK(reader.byte_index == 0U);
    R_TEST_CHECK(reader.hold == UINT64_C(0));
    R_TEST_CHECK(reader.bit_count == 0U);

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 3U);
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == UINT64_C(2));
    R_TEST_CHECK(reader.byte_index == 1U);
    R_TEST_CHECK(reader.hold == UINT64_C(0x19));
    R_TEST_CHECK(reader.bit_count == 5U);

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 5U);
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == UINT64_C(0x19));
    R_TEST_CHECK(reader.byte_index == 1U);
    R_TEST_CHECK(reader.hold == UINT64_C(0));
    R_TEST_CHECK(reader.bit_count == 0U);

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 8U);
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == UINT64_C(0x74));
    R_TEST_CHECK(reader.byte_index == 2U);
    return 0;
}

static int r_test_cross_byte_and_alignment(void) {
    const uint8_t input[] = {UINT8_C(0xca), UINT8_C(0x74), UINT8_C(0xa5)};
    RStdBitsLsbReader reader = {0};
    RStdBitsReadResult result;

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 12U);
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == UINT64_C(0x4ca));
    R_TEST_CHECK(reader.byte_index == 2U);
    R_TEST_CHECK(reader.hold == UINT64_C(7));
    R_TEST_CHECK(reader.bit_count == 4U);

    r_std_bits_align_byte(&reader);
    R_TEST_CHECK(reader.byte_index == 2U);
    R_TEST_CHECK(reader.hold == UINT64_C(0));
    R_TEST_CHECK(reader.bit_count == 0U);

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 8U);
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == UINT64_C(0xa5));
    R_TEST_CHECK(reader.byte_index == sizeof(input));
    return 0;
}

static int r_test_alignment_preserves_complete_buffered_bytes(void) {
    RStdBitsLsbReader reader = {
        .byte_index = 9U,
        .hold = UINT64_C(0x00000000000abcde),
        .bit_count = 20U,
    };

    r_std_bits_align_byte(&reader);
    R_TEST_CHECK(reader.byte_index == 9U);
    R_TEST_CHECK(reader.hold == UINT64_C(0x000000000000abcd));
    R_TEST_CHECK(reader.bit_count == 16U);
    return 0;
}

static int r_test_width_64_after_unaligned_read(void) {
    const uint8_t input[] = {
        UINT8_C(0x05),
        UINT8_C(0x01),
        UINT8_C(0x02),
        UINT8_C(0x03),
        UINT8_C(0x04),
        UINT8_C(0x05),
        UINT8_C(0x06),
        UINT8_C(0x07),
        UINT8_C(0x08),
    };
    RStdBitsLsbReader reader = {0};
    RStdBitsReadResult result;

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 3U);
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == UINT64_C(5));
    R_TEST_CHECK(reader.bit_count == 5U);

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 64U);
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == UINT64_C(0x00e0c0a080604020));
    R_TEST_CHECK(reader.byte_index == sizeof(input));
    R_TEST_CHECK(reader.hold == UINT64_C(1));
    R_TEST_CHECK(reader.bit_count == 5U);

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 5U);
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == UINT64_C(1));
    R_TEST_CHECK(reader.hold == UINT64_C(0));
    R_TEST_CHECK(reader.bit_count == 0U);
    return 0;
}

static uint64_t r_reference_bits(const uint8_t *input, uint8_t initial_offset, uint8_t width) {
    uint64_t value = UINT64_C(0);
    uint8_t index;

    for (index = 0U; index < width; ++index) {
        const size_t absolute = (size_t)initial_offset + (size_t)index;
        const uint8_t bit = (uint8_t)((input[absolute / 8U] >> (absolute % 8U)) & 1U);

        value |= (uint64_t)bit << index;
    }
    return value;
}

static int r_test_every_width_and_initial_bit_offset(void) {
    const uint8_t input[] = {
        UINT8_C(0xd3),
        UINT8_C(0x6a),
        UINT8_C(0x01),
        UINT8_C(0xfe),
        UINT8_C(0x55),
        UINT8_C(0x80),
        UINT8_C(0x7f),
        UINT8_C(0x24),
        UINT8_C(0xb9),
    };
    uint8_t initial_offset;

    for (initial_offset = 0U; initial_offset < 8U; ++initial_offset) {
        uint8_t width;

        for (width = 0U; width <= 64U; ++width) {
            const size_t final_position = (size_t)initial_offset + (size_t)width;
            const uint8_t final_offset = (uint8_t)(final_position % 8U);
            RStdBitsLsbReader reader = {0};
            RStdBitsReadResult result;

            if (initial_offset != 0U) {
                reader.byte_index = 1U;
                reader.hold = (uint64_t)(input[0] >> initial_offset);
                reader.bit_count = (uint8_t)(8U - initial_offset);
            }
            result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, width);
            R_TEST_CHECK(result.is_ok);
            R_TEST_CHECK(result.value == r_reference_bits(input, initial_offset, width));
            if (final_position == 0U) {
                R_TEST_CHECK(reader.byte_index == 0U);
                R_TEST_CHECK(reader.hold == UINT64_C(0));
                R_TEST_CHECK(reader.bit_count == 0U);
            } else if (final_offset == 0U) {
                R_TEST_CHECK(reader.byte_index == final_position / 8U);
                R_TEST_CHECK(reader.hold == UINT64_C(0));
                R_TEST_CHECK(reader.bit_count == 0U);
            } else {
                R_TEST_CHECK(reader.byte_index == (final_position / 8U) + 1U);
                R_TEST_CHECK(reader.hold == (uint64_t)(input[final_position / 8U] >> final_offset));
                R_TEST_CHECK(reader.bit_count == (uint8_t)(8U - final_offset));
            }
        }
    }
    return 0;
}

static int r_test_failures_are_transactional(void) {
    const uint8_t input[] = {UINT8_C(0x5a)};
    RStdBitsLsbReader reader = {0};
    RStdBitsLsbReader before;
    RStdBitsReadResult result;

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 3U);
    R_TEST_CHECK(result.is_ok);
    before = reader;

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 6U);
    R_TEST_CHECK(!result.is_ok);
    R_TEST_CHECK(result.error.code == R_STD_BITS_READ_ERROR_UNEXPECTED_END);
    R_TEST_CHECK(result.error.byte_index == sizeof(input));
    R_TEST_CHECK(memcmp(&reader, &before, sizeof(reader)) == 0);

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 65U);
    R_TEST_CHECK(!result.is_ok);
    R_TEST_CHECK(result.error.code == R_STD_BITS_READ_ERROR_INVALID_WIDTH);
    R_TEST_CHECK(result.error.byte_index == before.byte_index);
    R_TEST_CHECK(memcmp(&reader, &before, sizeof(reader)) == 0);
    return 0;
}

static int r_test_public_state_has_defined_width_64_behavior(void) {
    RStdBitsLsbReader reader = {
        .byte_index = 0U,
        .hold = UINT64_MAX,
        .bit_count = 64U,
    };
    RStdBitsReadResult result;

    result = r_std_bits_read((RStdBitsByteView){NULL, 0U}, &reader, 64U);
    R_TEST_CHECK(result.is_ok);
    R_TEST_CHECK(result.value == UINT64_MAX);
    R_TEST_CHECK(reader.byte_index == 0U);
    R_TEST_CHECK(reader.hold == UINT64_C(0));
    R_TEST_CHECK(reader.bit_count == 0U);
    return 0;
}

static int r_test_public_state_rejects_unrepresentable_buffer(void) {
    const RStdBitsLsbReader initial = {
        .byte_index = 7U,
        .hold = UINT64_C(0x1234),
        .bit_count = 65U,
    };
    RStdBitsLsbReader reader = initial;
    RStdBitsReadResult result;

    result = r_std_bits_read((RStdBitsByteView){NULL, 0U}, &reader, 1U);
    R_TEST_CHECK(!result.is_ok);
    R_TEST_CHECK(result.error.code == R_STD_BITS_READ_ERROR_INVALID_WIDTH);
    R_TEST_CHECK(result.error.byte_index == initial.byte_index);
    R_TEST_CHECK(memcmp(&reader, &initial, sizeof(reader)) == 0);
    return 0;
}

static int r_test_missing_position_after_view_end(void) {
    const uint8_t input[] = {UINT8_C(0xaa)};
    const RStdBitsLsbReader initial = {
        .byte_index = 3U,
        .hold = UINT64_C(0),
        .bit_count = 0U,
    };
    RStdBitsLsbReader reader = initial;
    RStdBitsReadResult result;

    result = r_std_bits_read((RStdBitsByteView){input, sizeof(input)}, &reader, 1U);
    R_TEST_CHECK(!result.is_ok);
    R_TEST_CHECK(result.error.code == R_STD_BITS_READ_ERROR_UNEXPECTED_END);
    R_TEST_CHECK(result.error.byte_index == initial.byte_index);
    R_TEST_CHECK(memcmp(&reader, &initial, sizeof(reader)) == 0);
    return 0;
}

int main(void) {
    if (r_test_zero_and_byte_reads() != 0) {
        return 1;
    }
    if (r_test_cross_byte_and_alignment() != 0) {
        return 1;
    }
    if (r_test_alignment_preserves_complete_buffered_bytes() != 0) {
        return 1;
    }
    if (r_test_width_64_after_unaligned_read() != 0) {
        return 1;
    }
    if (r_test_every_width_and_initial_bit_offset() != 0) {
        return 1;
    }
    if (r_test_failures_are_transactional() != 0) {
        return 1;
    }
    if (r_test_public_state_has_defined_width_64_behavior() != 0) {
        return 1;
    }
    if (r_test_public_state_rejects_unrepresentable_buffer() != 0) {
        return 1;
    }
    if (r_test_missing_position_after_view_end() != 0) {
        return 1;
    }
    return 0;
}
