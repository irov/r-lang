#include "r_std_hash.h"

#include <string.h>

static const uint32_t r_std_hash_sha256_constants[64] = {
    UINT32_C(0x428a2f98), UINT32_C(0x71374491), UINT32_C(0xb5c0fbcf), UINT32_C(0xe9b5dba5),
    UINT32_C(0x3956c25b), UINT32_C(0x59f111f1), UINT32_C(0x923f82a4), UINT32_C(0xab1c5ed5),
    UINT32_C(0xd807aa98), UINT32_C(0x12835b01), UINT32_C(0x243185be), UINT32_C(0x550c7dc3),
    UINT32_C(0x72be5d74), UINT32_C(0x80deb1fe), UINT32_C(0x9bdc06a7), UINT32_C(0xc19bf174),
    UINT32_C(0xe49b69c1), UINT32_C(0xefbe4786), UINT32_C(0x0fc19dc6), UINT32_C(0x240ca1cc),
    UINT32_C(0x2de92c6f), UINT32_C(0x4a7484aa), UINT32_C(0x5cb0a9dc), UINT32_C(0x76f988da),
    UINT32_C(0x983e5152), UINT32_C(0xa831c66d), UINT32_C(0xb00327c8), UINT32_C(0xbf597fc7),
    UINT32_C(0xc6e00bf3), UINT32_C(0xd5a79147), UINT32_C(0x06ca6351), UINT32_C(0x14292967),
    UINT32_C(0x27b70a85), UINT32_C(0x2e1b2138), UINT32_C(0x4d2c6dfc), UINT32_C(0x53380d13),
    UINT32_C(0x650a7354), UINT32_C(0x766a0abb), UINT32_C(0x81c2c92e), UINT32_C(0x92722c85),
    UINT32_C(0xa2bfe8a1), UINT32_C(0xa81a664b), UINT32_C(0xc24b8b70), UINT32_C(0xc76c51a3),
    UINT32_C(0xd192e819), UINT32_C(0xd6990624), UINT32_C(0xf40e3585), UINT32_C(0x106aa070),
    UINT32_C(0x19a4c116), UINT32_C(0x1e376c08), UINT32_C(0x2748774c), UINT32_C(0x34b0bcb5),
    UINT32_C(0x391c0cb3), UINT32_C(0x4ed8aa4a), UINT32_C(0x5b9cca4f), UINT32_C(0x682e6ff3),
    UINT32_C(0x748f82ee), UINT32_C(0x78a5636f), UINT32_C(0x84c87814), UINT32_C(0x8cc70208),
    UINT32_C(0x90befffa), UINT32_C(0xa4506ceb), UINT32_C(0xbef9a3f7), UINT32_C(0xc67178f2),
};

static uint32_t r_std_hash_sha256_rotate_right(uint32_t value, uint32_t count) {
    return (value >> count) | (value << (32U - count));
}

static uint32_t r_std_hash_sha256_load_be32(const uint8_t *source) {
    return ((uint32_t)source[0] << 24U) | ((uint32_t)source[1] << 16U) |
           ((uint32_t)source[2] << 8U) | (uint32_t)source[3];
}

static void r_std_hash_sha256_store_be32(uint8_t *destination, uint32_t value) {
    destination[0] = (uint8_t)(value >> 24U);
    destination[1] = (uint8_t)(value >> 16U);
    destination[2] = (uint8_t)(value >> 8U);
    destination[3] = (uint8_t)value;
}

static void r_std_hash_sha256_transform(uint32_t state[8], const uint8_t block[64]) {
    uint32_t schedule[64];
    uint32_t a = state[0];
    uint32_t b = state[1];
    uint32_t c = state[2];
    uint32_t d = state[3];
    uint32_t e = state[4];
    uint32_t f = state[5];
    uint32_t g = state[6];
    uint32_t h = state[7];
    size_t index;

    for (index = 0U; index < 16U; ++index) {
        schedule[index] = r_std_hash_sha256_load_be32(block + (index * 4U));
    }
    for (index = 16U; index < 64U; ++index) {
        const uint32_t lower = schedule[index - 15U];
        const uint32_t upper = schedule[index - 2U];
        const uint32_t sigma0 = r_std_hash_sha256_rotate_right(lower, 7U) ^
                                r_std_hash_sha256_rotate_right(lower, 18U) ^ (lower >> 3U);
        const uint32_t sigma1 = r_std_hash_sha256_rotate_right(upper, 17U) ^
                                r_std_hash_sha256_rotate_right(upper, 19U) ^ (upper >> 10U);

        schedule[index] = schedule[index - 16U] + sigma0 + schedule[index - 7U] + sigma1;
    }

    for (index = 0U; index < 64U; ++index) {
        const uint32_t sum1 = r_std_hash_sha256_rotate_right(e, 6U) ^
                              r_std_hash_sha256_rotate_right(e, 11U) ^
                              r_std_hash_sha256_rotate_right(e, 25U);
        const uint32_t choose = (e & f) ^ ((~e) & g);
        const uint32_t temporary1 =
            h + sum1 + choose + r_std_hash_sha256_constants[index] + schedule[index];
        const uint32_t sum0 = r_std_hash_sha256_rotate_right(a, 2U) ^
                              r_std_hash_sha256_rotate_right(a, 13U) ^
                              r_std_hash_sha256_rotate_right(a, 22U);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temporary2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temporary1;
        d = c;
        c = b;
        b = a;
        a = temporary1 + temporary2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

RStdHashSha256Digest r_std_hash_sha256(RStdHashByteView source) {
    uint32_t state[8] = {
        UINT32_C(0x6a09e667),
        UINT32_C(0xbb67ae85),
        UINT32_C(0x3c6ef372),
        UINT32_C(0xa54ff53a),
        UINT32_C(0x510e527f),
        UINT32_C(0x9b05688c),
        UINT32_C(0x1f83d9ab),
        UINT32_C(0x5be0cd19),
    };
    uint8_t tail[128] = {0};
    const uint8_t *cursor = source.data;
    size_t remaining = source.length;
    const uint64_t bit_length = (uint64_t)source.length * UINT64_C(8);
    size_t tail_length;
    size_t index;
    RStdHashSha256Digest result;

    while (remaining >= 64U) {
        r_std_hash_sha256_transform(state, cursor);
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
    r_std_hash_sha256_transform(state, tail);
    if (tail_length == 128U) {
        r_std_hash_sha256_transform(state, tail + 64U);
    }

    for (index = 0U; index < 8U; ++index) {
        r_std_hash_sha256_store_be32(result.bytes + (index * 4U), state[index]);
    }
    return result;
}
