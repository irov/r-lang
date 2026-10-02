#include "r_std_hash.h"

#include <string.h>

static const uint64_t r_std_hash_sha512_constants[80] = {
    UINT64_C(0x428a2f98d728ae22), UINT64_C(0x7137449123ef65cd), UINT64_C(0xb5c0fbcfec4d3b2f),
    UINT64_C(0xe9b5dba58189dbbc), UINT64_C(0x3956c25bf348b538), UINT64_C(0x59f111f1b605d019),
    UINT64_C(0x923f82a4af194f9b), UINT64_C(0xab1c5ed5da6d8118), UINT64_C(0xd807aa98a3030242),
    UINT64_C(0x12835b0145706fbe), UINT64_C(0x243185be4ee4b28c), UINT64_C(0x550c7dc3d5ffb4e2),
    UINT64_C(0x72be5d74f27b896f), UINT64_C(0x80deb1fe3b1696b1), UINT64_C(0x9bdc06a725c71235),
    UINT64_C(0xc19bf174cf692694), UINT64_C(0xe49b69c19ef14ad2), UINT64_C(0xefbe4786384f25e3),
    UINT64_C(0x0fc19dc68b8cd5b5), UINT64_C(0x240ca1cc77ac9c65), UINT64_C(0x2de92c6f592b0275),
    UINT64_C(0x4a7484aa6ea6e483), UINT64_C(0x5cb0a9dcbd41fbd4), UINT64_C(0x76f988da831153b5),
    UINT64_C(0x983e5152ee66dfab), UINT64_C(0xa831c66d2db43210), UINT64_C(0xb00327c898fb213f),
    UINT64_C(0xbf597fc7beef0ee4), UINT64_C(0xc6e00bf33da88fc2), UINT64_C(0xd5a79147930aa725),
    UINT64_C(0x06ca6351e003826f), UINT64_C(0x142929670a0e6e70), UINT64_C(0x27b70a8546d22ffc),
    UINT64_C(0x2e1b21385c26c926), UINT64_C(0x4d2c6dfc5ac42aed), UINT64_C(0x53380d139d95b3df),
    UINT64_C(0x650a73548baf63de), UINT64_C(0x766a0abb3c77b2a8), UINT64_C(0x81c2c92e47edaee6),
    UINT64_C(0x92722c851482353b), UINT64_C(0xa2bfe8a14cf10364), UINT64_C(0xa81a664bbc423001),
    UINT64_C(0xc24b8b70d0f89791), UINT64_C(0xc76c51a30654be30), UINT64_C(0xd192e819d6ef5218),
    UINT64_C(0xd69906245565a910), UINT64_C(0xf40e35855771202a), UINT64_C(0x106aa07032bbd1b8),
    UINT64_C(0x19a4c116b8d2d0c8), UINT64_C(0x1e376c085141ab53), UINT64_C(0x2748774cdf8eeb99),
    UINT64_C(0x34b0bcb5e19b48a8), UINT64_C(0x391c0cb3c5c95a63), UINT64_C(0x4ed8aa4ae3418acb),
    UINT64_C(0x5b9cca4f7763e373), UINT64_C(0x682e6ff3d6b2b8a3), UINT64_C(0x748f82ee5defb2fc),
    UINT64_C(0x78a5636f43172f60), UINT64_C(0x84c87814a1f0ab72), UINT64_C(0x8cc702081a6439ec),
    UINT64_C(0x90befffa23631e28), UINT64_C(0xa4506cebde82bde9), UINT64_C(0xbef9a3f7b2c67915),
    UINT64_C(0xc67178f2e372532b), UINT64_C(0xca273eceea26619c), UINT64_C(0xd186b8c721c0c207),
    UINT64_C(0xeada7dd6cde0eb1e), UINT64_C(0xf57d4f7fee6ed178), UINT64_C(0x06f067aa72176fba),
    UINT64_C(0x0a637dc5a2c898a6), UINT64_C(0x113f9804bef90dae), UINT64_C(0x1b710b35131c471b),
    UINT64_C(0x28db77f523047d84), UINT64_C(0x32caab7b40c72493), UINT64_C(0x3c9ebe0a15c9bebc),
    UINT64_C(0x431d67c49c100d4c), UINT64_C(0x4cc5d4becb3e42b6), UINT64_C(0x597f299cfc657e2a),
    UINT64_C(0x5fcb6fab3ad6faec), UINT64_C(0x6c44198c4a475817),
};

static uint64_t r_std_hash_sha512_rotate_right(uint64_t value, uint32_t count) {
    return (value >> count) | (value << (64U - count));
}

static uint64_t r_std_hash_sha512_load_be64(const uint8_t *source) {
    return ((uint64_t)source[0] << 56U) | ((uint64_t)source[1] << 48U) |
           ((uint64_t)source[2] << 40U) | ((uint64_t)source[3] << 32U) |
           ((uint64_t)source[4] << 24U) | ((uint64_t)source[5] << 16U) |
           ((uint64_t)source[6] << 8U) | (uint64_t)source[7];
}

static void r_std_hash_sha512_store_be64(uint8_t *destination, uint64_t value) {
    destination[0] = (uint8_t)(value >> 56U);
    destination[1] = (uint8_t)(value >> 48U);
    destination[2] = (uint8_t)(value >> 40U);
    destination[3] = (uint8_t)(value >> 32U);
    destination[4] = (uint8_t)(value >> 24U);
    destination[5] = (uint8_t)(value >> 16U);
    destination[6] = (uint8_t)(value >> 8U);
    destination[7] = (uint8_t)value;
}

static void r_std_hash_sha512_transform(uint64_t state[8], const uint8_t block[128]) {
    uint64_t schedule[80];
    uint64_t a = state[0];
    uint64_t b = state[1];
    uint64_t c = state[2];
    uint64_t d = state[3];
    uint64_t e = state[4];
    uint64_t f = state[5];
    uint64_t g = state[6];
    uint64_t h = state[7];
    size_t index;

    for (index = 0U; index < 16U; ++index) {
        schedule[index] = r_std_hash_sha512_load_be64(block + (index * 8U));
    }
    for (index = 16U; index < 80U; ++index) {
        const uint64_t lower = schedule[index - 15U];
        const uint64_t upper = schedule[index - 2U];
        const uint64_t sigma0 = r_std_hash_sha512_rotate_right(lower, 1U) ^
                                r_std_hash_sha512_rotate_right(lower, 8U) ^ (lower >> 7U);
        const uint64_t sigma1 = r_std_hash_sha512_rotate_right(upper, 19U) ^
                                r_std_hash_sha512_rotate_right(upper, 61U) ^ (upper >> 6U);

        schedule[index] = schedule[index - 16U] + sigma0 + schedule[index - 7U] + sigma1;
    }

    for (index = 0U; index < 80U; ++index) {
        const uint64_t sum1 = r_std_hash_sha512_rotate_right(e, 14U) ^
                              r_std_hash_sha512_rotate_right(e, 18U) ^
                              r_std_hash_sha512_rotate_right(e, 41U);
        const uint64_t choose = (e & f) ^ ((~e) & g);
        const uint64_t temporary1 =
            h + sum1 + choose + r_std_hash_sha512_constants[index] + schedule[index];
        const uint64_t sum0 = r_std_hash_sha512_rotate_right(a, 28U) ^
                              r_std_hash_sha512_rotate_right(a, 34U) ^
                              r_std_hash_sha512_rotate_right(a, 39U);
        const uint64_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint64_t temporary2 = sum0 + majority;

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

RStdHashSha512Digest r_std_hash_sha512(RStdHashByteView source) {
    uint64_t state[8] = {
        UINT64_C(0x6a09e667f3bcc908),
        UINT64_C(0xbb67ae8584caa73b),
        UINT64_C(0x3c6ef372fe94f82b),
        UINT64_C(0xa54ff53a5f1d36f1),
        UINT64_C(0x510e527fade682d1),
        UINT64_C(0x9b05688c2b3e6c1f),
        UINT64_C(0x1f83d9abfb41bd6b),
        UINT64_C(0x5be0cd19137e2179),
    };
    uint8_t tail[256] = {0};
    const uint8_t *cursor = source.data;
    size_t remaining = source.length;
    const uint64_t byte_length = (uint64_t)source.length;
    const uint64_t bit_length_high = byte_length >> 61U;
    const uint64_t bit_length_low = byte_length << 3U;
    size_t tail_length;
    size_t index;
    RStdHashSha512Digest result;

    while (remaining >= 128U) {
        r_std_hash_sha512_transform(state, cursor);
        cursor += 128U;
        remaining -= 128U;
    }
    if (remaining != 0U) {
        (void)memcpy(tail, cursor, remaining);
    }
    tail[remaining] = UINT8_C(0x80);
    tail_length = remaining < 112U ? 128U : 256U;
    r_std_hash_sha512_store_be64(tail + tail_length - 16U, bit_length_high);
    r_std_hash_sha512_store_be64(tail + tail_length - 8U, bit_length_low);
    r_std_hash_sha512_transform(state, tail);
    if (tail_length == 256U) {
        r_std_hash_sha512_transform(state, tail + 128U);
    }

    for (index = 0U; index < 8U; ++index) {
        r_std_hash_sha512_store_be64(result.bytes + (index * 8U), state[index]);
    }
    return result;
}
