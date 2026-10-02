#include "r_std_hash.h"

#include <string.h>

static const uint32_t r_std_hash_md5_constants[64] = {
    UINT32_C(0xd76aa478), UINT32_C(0xe8c7b756), UINT32_C(0x242070db), UINT32_C(0xc1bdceee),
    UINT32_C(0xf57c0faf), UINT32_C(0x4787c62a), UINT32_C(0xa8304613), UINT32_C(0xfd469501),
    UINT32_C(0x698098d8), UINT32_C(0x8b44f7af), UINT32_C(0xffff5bb1), UINT32_C(0x895cd7be),
    UINT32_C(0x6b901122), UINT32_C(0xfd987193), UINT32_C(0xa679438e), UINT32_C(0x49b40821),
    UINT32_C(0xf61e2562), UINT32_C(0xc040b340), UINT32_C(0x265e5a51), UINT32_C(0xe9b6c7aa),
    UINT32_C(0xd62f105d), UINT32_C(0x02441453), UINT32_C(0xd8a1e681), UINT32_C(0xe7d3fbc8),
    UINT32_C(0x21e1cde6), UINT32_C(0xc33707d6), UINT32_C(0xf4d50d87), UINT32_C(0x455a14ed),
    UINT32_C(0xa9e3e905), UINT32_C(0xfcefa3f8), UINT32_C(0x676f02d9), UINT32_C(0x8d2a4c8a),
    UINT32_C(0xfffa3942), UINT32_C(0x8771f681), UINT32_C(0x6d9d6122), UINT32_C(0xfde5380c),
    UINT32_C(0xa4beea44), UINT32_C(0x4bdecfa9), UINT32_C(0xf6bb4b60), UINT32_C(0xbebfbc70),
    UINT32_C(0x289b7ec6), UINT32_C(0xeaa127fa), UINT32_C(0xd4ef3085), UINT32_C(0x04881d05),
    UINT32_C(0xd9d4d039), UINT32_C(0xe6db99e5), UINT32_C(0x1fa27cf8), UINT32_C(0xc4ac5665),
    UINT32_C(0xf4292244), UINT32_C(0x432aff97), UINT32_C(0xab9423a7), UINT32_C(0xfc93a039),
    UINT32_C(0x655b59c3), UINT32_C(0x8f0ccc92), UINT32_C(0xffeff47d), UINT32_C(0x85845dd1),
    UINT32_C(0x6fa87e4f), UINT32_C(0xfe2ce6e0), UINT32_C(0xa3014314), UINT32_C(0x4e0811a1),
    UINT32_C(0xf7537e82), UINT32_C(0xbd3af235), UINT32_C(0x2ad7d2bb), UINT32_C(0xeb86d391),
};

static const uint8_t r_std_hash_md5_rotations[64] = {
    7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U, 7U, 12U, 17U, 22U,
    5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U, 5U, 9U,  14U, 20U,
    4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U, 4U, 11U, 16U, 23U,
    6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U, 6U, 10U, 15U, 21U,
};

static uint32_t r_std_hash_md5_rotate_left(uint32_t value, uint8_t count) {
    return (value << count) | (value >> (32U - count));
}

static uint32_t r_std_hash_md5_load_le32(const uint8_t *source) {
    return (uint32_t)source[0] | ((uint32_t)source[1] << 8U) | ((uint32_t)source[2] << 16U) |
           ((uint32_t)source[3] << 24U);
}

static void r_std_hash_md5_store_le32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8U);
    destination[2] = (uint8_t)(value >> 16U);
    destination[3] = (uint8_t)(value >> 24U);
}

static void r_std_hash_md5_transform(uint32_t state[4], const uint8_t block[64]) {
    uint32_t words[16];
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    size_t index;

    for (index = 0U; index < 16U; ++index) {
        words[index] = r_std_hash_md5_load_le32(block + (index * 4U));
    }

    for (index = 0U; index < 64U; ++index) {
        uint32_t mixed;
        size_t word_index;
        const uint32_t previous_d = d;

        if (index < 16U) {
            mixed = (b & c) | ((~b) & d);
            word_index = index;
        } else if (index < 32U) {
            mixed = (d & b) | ((~d) & c);
            word_index = ((5U * index) + 1U) % 16U;
        } else if (index < 48U) {
            mixed = b ^ c ^ d;
            word_index = ((3U * index) + 5U) % 16U;
        } else {
            mixed = c ^ (b | (~d));
            word_index = (7U * index) % 16U;
        }

        d = c;
        c = b;
        b += r_std_hash_md5_rotate_left(a + mixed + r_std_hash_md5_constants[index] +
                                            words[word_index],
                                        r_std_hash_md5_rotations[index]);
        a = previous_d;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
}

RStdHashMd5Digest r_std_hash_md5(RStdHashByteView source) {
    uint32_t state[4] = {
        UINT32_C(0x67452301),
        UINT32_C(0xefcdab89),
        UINT32_C(0x98badcfe),
        UINT32_C(0x10325476),
    };
    uint8_t tail[128] = {0};
    const uint8_t *cursor = source.data;
    size_t remaining = source.length;
    const uint64_t bit_length = (uint64_t)source.length * UINT64_C(8);
    size_t tail_length;
    size_t index;
    RStdHashMd5Digest result;

    while (remaining >= 64U) {
        r_std_hash_md5_transform(state, cursor);
        cursor += 64U;
        remaining -= 64U;
    }
    if (remaining != 0U) {
        (void)memcpy(tail, cursor, remaining);
    }
    tail[remaining] = UINT8_C(0x80);
    tail_length = remaining < 56U ? 64U : 128U;
    for (index = 0U; index < 8U; ++index) {
        tail[tail_length - 8U + index] = (uint8_t)(bit_length >> (index * 8U));
    }

    r_std_hash_md5_transform(state, tail);
    if (tail_length == 128U) {
        r_std_hash_md5_transform(state, tail + 64U);
    }

    for (index = 0U; index < 4U; ++index) {
        r_std_hash_md5_store_le32(result.bytes + (index * 4U), state[index]);
    }
    return result;
}
