#include "r_std_hash.h"

#include <string.h>

static uint32_t r_std_hash_sha1_rotate_left(uint32_t value, uint32_t count) {
    return (value << count) | (value >> (32U - count));
}

static uint32_t r_std_hash_sha1_load_be32(const uint8_t *source) {
    return ((uint32_t)source[0] << 24U) | ((uint32_t)source[1] << 16U) |
           ((uint32_t)source[2] << 8U) | (uint32_t)source[3];
}

static void r_std_hash_sha1_store_be32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)(value >> 24U);
    destination[1] = (uint8_t)(value >> 16U);
    destination[2] = (uint8_t)(value >> 8U);
    destination[3] = (uint8_t)value;
}

static void r_std_hash_sha1_transform(uint32_t state[5], const uint8_t block[64]) {
    uint32_t schedule[80];
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    size_t index;

    for (index = 0U; index < 16U; ++index) {
        schedule[index] = r_std_hash_sha1_load_be32(block + (index * 4U));
    }
    for (index = 16U; index < 80U; ++index) {
        schedule[index] =
            r_std_hash_sha1_rotate_left(schedule[index - 3U] ^ schedule[index - 8U] ^
                                            schedule[index - 14U] ^ schedule[index - 16U],
                                        1U);
    }

    for (index = 0U; index < 80U; ++index) {
        uint32_t choose;
        uint32_t constant;
        uint32_t temporary;

        if (index < 20U) {
            choose = (b & c) | ((~b) & d);
            constant = UINT32_C(0x5a827999);
        } else if (index < 40U) {
            choose = b ^ c ^ d;
            constant = UINT32_C(0x6ed9eba1);
        } else if (index < 60U) {
            choose = (b & c) | (b & d) | (c & d);
            constant = UINT32_C(0x8f1bbcdc);
        } else {
            choose = b ^ c ^ d;
            constant = UINT32_C(0xca62c1d6);
        }
        temporary = r_std_hash_sha1_rotate_left(a, 5U) + choose + e + constant + schedule[index];
        e = d;
        d = c;
        c = r_std_hash_sha1_rotate_left(b, 30U);
        b = a;
        a = temporary;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
}

RStdHashSha1Digest r_std_hash_sha1(RStdHashByteView source) {
    uint32_t state[5] = {
        UINT32_C(0x67452301),
        UINT32_C(0xefcdab89),
        UINT32_C(0x98badcfe),
        UINT32_C(0x10325476),
        UINT32_C(0xc3d2e1f0),
    };
    uint8_t tail[128] = {0};
    const uint8_t *cursor = source.data;
    size_t remaining = source.length;
    const uint64_t bit_length = (uint64_t)source.length * UINT64_C(8);
    size_t tail_length;
    RStdHashSha1Digest result;

    while (remaining >= 64U) {
        r_std_hash_sha1_transform(state, cursor);
        cursor += 64U;
        remaining -= 64U;
    }
    if (remaining != 0U) {
        (void)memcpy(tail, cursor, remaining);
    }
    tail[remaining] = UINT8_C(0x80);
    tail_length = remaining < 56U ? 64U : 128U;
    tail[tail_length - 8U] = (uint8_t)(bit_length >> 56U);
    tail[tail_length - 7U] = (uint8_t)(bit_length >> 48U);
    tail[tail_length - 6U] = (uint8_t)(bit_length >> 40U);
    tail[tail_length - 5U] = (uint8_t)(bit_length >> 32U);
    tail[tail_length - 4U] = (uint8_t)(bit_length >> 24U);
    tail[tail_length - 3U] = (uint8_t)(bit_length >> 16U);
    tail[tail_length - 2U] = (uint8_t)(bit_length >> 8U);
    tail[tail_length - 1U] = (uint8_t)bit_length;
    r_std_hash_sha1_transform(state, tail);
    if (tail_length == 128U) {
        r_std_hash_sha1_transform(state, tail + 64U);
    }

    r_std_hash_sha1_store_be32(result.bytes, state[0]);
    r_std_hash_sha1_store_be32(result.bytes + 4U, state[1]);
    r_std_hash_sha1_store_be32(result.bytes + 8U, state[2]);
    r_std_hash_sha1_store_be32(result.bytes + 12U, state[3]);
    r_std_hash_sha1_store_be32(result.bytes + 16U, state[4]);
    return result;
}
