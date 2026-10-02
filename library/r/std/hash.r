module std.hash;

/* R-SLIB-BYTES-0010: the R part of std.hash, loaded by `import std.hash;`: SHA-256 and SHA-512
   of FIPS PUB 180-4 over data given in pieces, and HMAC of RFC 2104 over them. */

const u32[64] SHA256_ROUNDS = {
    0x428a2f98u32, 0x71374491u32, 0xb5c0fbcfu32, 0xe9b5dba5u32, 0x3956c25bu32, 0x59f111f1u32,
    0x923f82a4u32, 0xab1c5ed5u32, 0xd807aa98u32, 0x12835b01u32, 0x243185beu32, 0x550c7dc3u32,
    0x72be5d74u32, 0x80deb1feu32, 0x9bdc06a7u32, 0xc19bf174u32, 0xe49b69c1u32, 0xefbe4786u32,
    0x0fc19dc6u32, 0x240ca1ccu32, 0x2de92c6fu32, 0x4a7484aau32, 0x5cb0a9dcu32, 0x76f988dau32,
    0x983e5152u32, 0xa831c66du32, 0xb00327c8u32, 0xbf597fc7u32, 0xc6e00bf3u32, 0xd5a79147u32,
    0x06ca6351u32, 0x14292967u32, 0x27b70a85u32, 0x2e1b2138u32, 0x4d2c6dfcu32, 0x53380d13u32,
    0x650a7354u32, 0x766a0abbu32, 0x81c2c92eu32, 0x92722c85u32, 0xa2bfe8a1u32, 0xa81a664bu32,
    0xc24b8b70u32, 0xc76c51a3u32, 0xd192e819u32, 0xd6990624u32, 0xf40e3585u32, 0x106aa070u32,
    0x19a4c116u32, 0x1e376c08u32, 0x2748774cu32, 0x34b0bcb5u32, 0x391c0cb3u32, 0x4ed8aa4au32,
    0x5b9cca4fu32, 0x682e6ff3u32, 0x748f82eeu32, 0x78a5636fu32, 0x84c87814u32, 0x8cc70208u32,
    0x90befffau32, 0xa4506cebu32, 0xbef9a3f7u32, 0xc67178f2u32,
};

const u64[80] SHA512_ROUNDS = {
    0x428a2f98d728ae22u64, 0x7137449123ef65cdu64, 0xb5c0fbcfec4d3b2fu64, 0xe9b5dba58189dbbcu64,
    0x3956c25bf348b538u64, 0x59f111f1b605d019u64, 0x923f82a4af194f9bu64, 0xab1c5ed5da6d8118u64,
    0xd807aa98a3030242u64, 0x12835b0145706fbeu64, 0x243185be4ee4b28cu64, 0x550c7dc3d5ffb4e2u64,
    0x72be5d74f27b896fu64, 0x80deb1fe3b1696b1u64, 0x9bdc06a725c71235u64, 0xc19bf174cf692694u64,
    0xe49b69c19ef14ad2u64, 0xefbe4786384f25e3u64, 0x0fc19dc68b8cd5b5u64, 0x240ca1cc77ac9c65u64,
    0x2de92c6f592b0275u64, 0x4a7484aa6ea6e483u64, 0x5cb0a9dcbd41fbd4u64, 0x76f988da831153b5u64,
    0x983e5152ee66dfabu64, 0xa831c66d2db43210u64, 0xb00327c898fb213fu64, 0xbf597fc7beef0ee4u64,
    0xc6e00bf33da88fc2u64, 0xd5a79147930aa725u64, 0x06ca6351e003826fu64, 0x142929670a0e6e70u64,
    0x27b70a8546d22ffcu64, 0x2e1b21385c26c926u64, 0x4d2c6dfc5ac42aedu64, 0x53380d139d95b3dfu64,
    0x650a73548baf63deu64, 0x766a0abb3c77b2a8u64, 0x81c2c92e47edaee6u64, 0x92722c851482353bu64,
    0xa2bfe8a14cf10364u64, 0xa81a664bbc423001u64, 0xc24b8b70d0f89791u64, 0xc76c51a30654be30u64,
    0xd192e819d6ef5218u64, 0xd69906245565a910u64, 0xf40e35855771202au64, 0x106aa07032bbd1b8u64,
    0x19a4c116b8d2d0c8u64, 0x1e376c085141ab53u64, 0x2748774cdf8eeb99u64, 0x34b0bcb5e19b48a8u64,
    0x391c0cb3c5c95a63u64, 0x4ed8aa4ae3418acbu64, 0x5b9cca4f7763e373u64, 0x682e6ff3d6b2b8a3u64,
    0x748f82ee5defb2fcu64, 0x78a5636f43172f60u64, 0x84c87814a1f0ab72u64, 0x8cc702081a6439ecu64,
    0x90befffa23631e28u64, 0xa4506cebde82bde9u64, 0xbef9a3f7b2c67915u64, 0xc67178f2e372532bu64,
    0xca273eceea26619cu64, 0xd186b8c721c0c207u64, 0xeada7dd6cde0eb1eu64, 0xf57d4f7fee6ed178u64,
    0x06f067aa72176fbau64, 0x0a637dc5a2c898a6u64, 0x113f9804bef90daeu64, 0x1b710b35131c471bu64,
    0x28db77f523047d84u64, 0x32caab7b40c72493u64, 0x3c9ebe0a15c9bebcu64, 0x431d67c49c100d4cu64,
    0x4cc5d4becb3e42b6u64, 0x597f299cfc657e2au64, 0x5fcb6fab3ad6faecu64, 0x6c44198c4a475817u64,
};

protected u32 rotate32(u32 value, u32 count) {
    return (value >> count) | (value << (32u32 - count));
}

protected u64 rotate64(u64 value, u64 count) {
    return (value >> count) | (value << (64u64 - count));
}

/* R-SLIB-BYTES-0010: SHA-256 over data in pieces. The block holds the bytes of an unfinished
   64-byte block; length counts every byte given. */
struct sha256_state {
    protected u32[8] words;
    protected u8[64] block;
    protected usize filled;
    protected u64 length;
};

sha256_state sha256_state::create() {
    return sha256_state {
        .words = {0x6a09e667u32, 0xbb67ae85u32, 0x3c6ef372u32, 0xa54ff53au32, 0x510e527fu32,
                  0x9b05688cu32, 0x1f83d9abu32, 0x5be0cd19u32},
        .block = {}, .filled = 0usize, .length = 0u64,
    };
}

/* One compression of a full block into the chaining words. */
protected void sha256_compress((u32[8])* words, const u8[] block) {
    u32[64] schedule = {};
    for (usize t = 0usize; t < 16usize; t += 1usize) {
        schedule[t] = ((block[t * 4usize] as u32) << 24u32) |
                      ((block[t * 4usize + 1usize] as u32) << 16u32) |
                      ((block[t * 4usize + 2usize] as u32) << 8u32) |
                      (block[t * 4usize + 3usize] as u32);
    }
    for (usize t = 16usize; t < 64usize; t += 1usize) {
        u32 early = schedule[t - 15usize];
        u32 late = schedule[t - 2usize];
        u32 small0 = rotate32(early, 7u32) ^ rotate32(early, 18u32) ^ (early >> 3u32);
        u32 small1 = rotate32(late, 17u32) ^ rotate32(late, 19u32) ^ (late >> 10u32);
        schedule[t] = core::wrapping_add_u32(
            core::wrapping_add_u32(small1, schedule[t - 7usize]),
            core::wrapping_add_u32(small0, schedule[t - 16usize]));
    }
    u32[8] v = *words;
    for (usize t = 0usize; t < 64usize; t += 1usize) {
        u32 e = v[4];
        u32 a = v[0];
        u32 big1 = rotate32(e, 6u32) ^ rotate32(e, 11u32) ^ rotate32(e, 25u32);
        u32 choose = (e & v[5]) ^ (~e & v[6]);
        u32 first = core::wrapping_add_u32(
            core::wrapping_add_u32(core::wrapping_add_u32(v[7], big1),
                                   core::wrapping_add_u32(choose, SHA256_ROUNDS[t])),
            schedule[t]);
        u32 big0 = rotate32(a, 2u32) ^ rotate32(a, 13u32) ^ rotate32(a, 22u32);
        u32 majority = (a & v[1]) ^ (a & v[2]) ^ (v[1] & v[2]);
        u32 second = core::wrapping_add_u32(big0, majority);
        v[7] = v[6];
        v[6] = v[5];
        v[5] = e;
        v[4] = core::wrapping_add_u32(v[3], first);
        v[3] = v[2];
        v[2] = v[1];
        v[1] = a;
        v[0] = core::wrapping_add_u32(first, second);
    }
    for (usize index = 0usize; index < 8usize; index += 1usize) {
        (*words)[index] = core::wrapping_add_u32((*words)[index], v[index]);
    }
}

void sha256_state::update(sha256_state* this, const u8[] data) {
    usize at = 0usize;
    usize total = len(data);
    this->length = core::wrapping_add_u64(this->length, total as u64);
    while (at < total) {
        usize take = 64usize - this->filled;
        if (take > total - at) { take = total - at; }
        for (usize index = 0usize; index < take; index += 1usize) {
            this->block[this->filled + index] = data[at + index];
        }
        this->filled += take;
        at += take;
        if (this->filled == 64usize) {
            sha256_compress(&this->words, this->block);
            this->filled = 0usize;
        }
    }
}

std.hash::sha256_digest sha256_state::finish(sha256_state this) {
    u64 bits = core::wrapping_mul_u64(this.length, 8u64);
    u64 rest = bits;
    u8[72] tail = {};
    tail[0] = 128u8;
    usize padding = 64usize - (this.filled + 8usize) % 64usize;
    for (usize index = 8usize; index > 0usize; index -= 1usize) {
        tail[padding + index - 1usize] = (rest & 255u64) as u8;
        rest = rest >> 8u64;
    }
    this.update(tail[0usize..padding + 8usize]);
    std.hash::sha256_digest digest = {};
    for (usize index = 0usize; index < 8usize; index += 1usize) {
        u32 word = this.words[index];
        digest.bytes[index * 4usize] = (word >> 24u32) as u8;
        digest.bytes[index * 4usize + 1usize] = ((word >> 16u32) & 255u32) as u8;
        digest.bytes[index * 4usize + 2usize] = ((word >> 8u32) & 255u32) as u8;
        digest.bytes[index * 4usize + 3usize] = (word & 255u32) as u8;
    }
    return digest;
}

/* R-SLIB-BYTES-0010: SHA-512 over data in pieces; the length counts bytes modulo 2^64, which
   covers every slice (R-SLIB-BYTES-0007). */
struct sha512_state {
    protected u64[8] words;
    protected u8[128] block;
    protected usize filled;
    protected u64 length;
};

sha512_state sha512_state::create() {
    return sha512_state {
        .words = {0x6a09e667f3bcc908u64, 0xbb67ae8584caa73bu64, 0x3c6ef372fe94f82bu64,
                  0xa54ff53a5f1d36f1u64, 0x510e527fade682d1u64, 0x9b05688c2b3e6c1fu64,
                  0x1f83d9abfb41bd6bu64, 0x5be0cd19137e2179u64},
        .block = {}, .filled = 0usize, .length = 0u64,
    };
}

protected void sha512_compress((u64[8])* words, const u8[] block) {
    u64[80] schedule = {};
    for (usize t = 0usize; t < 16usize; t += 1usize) {
        u64 word = 0u64;
        for (usize index = 0usize; index < 8usize; index += 1usize) {
            word = (word << 8u64) | (block[t * 8usize + index] as u64);
        }
        schedule[t] = word;
    }
    for (usize t = 16usize; t < 80usize; t += 1usize) {
        u64 early = schedule[t - 15usize];
        u64 late = schedule[t - 2usize];
        u64 small0 = rotate64(early, 1u64) ^ rotate64(early, 8u64) ^ (early >> 7u64);
        u64 small1 = rotate64(late, 19u64) ^ rotate64(late, 61u64) ^ (late >> 6u64);
        schedule[t] = core::wrapping_add_u64(
            core::wrapping_add_u64(small1, schedule[t - 7usize]),
            core::wrapping_add_u64(small0, schedule[t - 16usize]));
    }
    u64[8] v = *words;
    for (usize t = 0usize; t < 80usize; t += 1usize) {
        u64 e = v[4];
        u64 a = v[0];
        u64 big1 = rotate64(e, 14u64) ^ rotate64(e, 18u64) ^ rotate64(e, 41u64);
        u64 choose = (e & v[5]) ^ (~e & v[6]);
        u64 first = core::wrapping_add_u64(
            core::wrapping_add_u64(core::wrapping_add_u64(v[7], big1),
                                   core::wrapping_add_u64(choose, SHA512_ROUNDS[t])),
            schedule[t]);
        u64 big0 = rotate64(a, 28u64) ^ rotate64(a, 34u64) ^ rotate64(a, 39u64);
        u64 majority = (a & v[1]) ^ (a & v[2]) ^ (v[1] & v[2]);
        u64 second = core::wrapping_add_u64(big0, majority);
        v[7] = v[6];
        v[6] = v[5];
        v[5] = e;
        v[4] = core::wrapping_add_u64(v[3], first);
        v[3] = v[2];
        v[2] = v[1];
        v[1] = a;
        v[0] = core::wrapping_add_u64(first, second);
    }
    for (usize index = 0usize; index < 8usize; index += 1usize) {
        (*words)[index] = core::wrapping_add_u64((*words)[index], v[index]);
    }
}

void sha512_state::update(sha512_state* this, const u8[] data) {
    usize at = 0usize;
    usize total = len(data);
    this->length = core::wrapping_add_u64(this->length, total as u64);
    while (at < total) {
        usize take = 128usize - this->filled;
        if (take > total - at) { take = total - at; }
        for (usize index = 0usize; index < take; index += 1usize) {
            this->block[this->filled + index] = data[at + index];
        }
        this->filled += take;
        at += take;
        if (this->filled == 128usize) {
            sha512_compress(&this->words, this->block);
            this->filled = 0usize;
        }
    }
}

std.hash::sha512_digest sha512_state::finish(sha512_state this) {
    u64 bits = core::wrapping_mul_u64(this.length, 8u64);
    u64 low = bits;
    u64 high = this.length >> 61u64;
    u8[144] tail = {};
    tail[0] = 128u8;
    usize padding = 128usize - (this.filled + 16usize) % 128usize;
    for (usize index = 8usize; index > 0usize; index -= 1usize) {
        tail[padding + index - 1usize] = (high & 255u64) as u8;
        tail[padding + 8usize + index - 1usize] = (low & 255u64) as u8;
        high = high >> 8u64;
        low = low >> 8u64;
    }
    this.update(tail[0usize..padding + 16usize]);
    std.hash::sha512_digest digest = {};
    for (usize index = 0usize; index < 8usize; index += 1usize) {
        u64 word = this.words[index];
        for (usize part = 0usize; part < 8usize; part += 1usize) {
            u64 shift = (56usize - part * 8usize) as u64;
            digest.bytes[index * 8usize + part] = ((word >> shift) & 255u64) as u8;
        }
    }
    return digest;
}

/* R-SLIB-BYTES-0010: SHA-384 over data in pieces: SHA-512 from its own initial words, whose
   output is cut to the first 48 bytes (FIPS PUB 180-4 sections 5.3.4 and 6.5). */
struct sha384_state {
    protected sha512_state inner;
};

sha384_state sha384_state::create() {
    sha512_state inner = sha512_state::create();
    u64[8] words = {0xcbbb9d5dc1059ed8u64, 0x629a292a367cd507u64, 0x9159015a3070dd17u64,
                    0x152fecd8f70e5939u64, 0x67332667ffc00b31u64, 0x8eb44a8768581511u64,
                    0xdb0c2e0d64f98fa7u64, 0x47b5481dbefa4fa4u64};
    inner.words = words;
    return sha384_state {.inner = inner};
}

void sha384_state::update(sha384_state* this, const u8[] data) {
    this->inner.update(data);
}

u8[48] sha384_state::finish(sha384_state this) {
    std.hash::sha512_digest full = this.inner.finish();
    u8[64] all = full.bytes;
    u8[48] digest = {};
    for (usize index = 0usize; index < 48usize; index += 1usize) {
        digest[index] = all[index];
    }
    return digest;
}

/* SHA-384 of one piece of data. */
u8[48] sha384(const u8[] data) {
    sha384_state state = sha384_state::create();
    state.update(data);
    return state.finish();
}

/* R-SLIB-BYTES-0011: HMAC of RFC 2104. A key longer than the block is hashed first; the key
   padded with zeros to the block is combined with 0x36 for the inner hash and with 0x5c for the
   outer one. The padded keys are erased before the result returns. */
std.hash::sha256_digest hmac_sha256(const u8[] key, const u8[] message) {
    u8[64] inner_key = {};
    u8[64] outer_key = {};
    if (len(key) > 64usize) {
        std.hash::sha256_digest reduced = std.hash::sha256(key);
        u8[32] reduced_bytes = reduced.bytes;
        for (usize index = 0usize; index < 32usize; index += 1usize) {
            inner_key[index] = reduced_bytes[index];
        }
    } else {
        for (usize index = 0usize; index < len(key); index += 1usize) {
            inner_key[index] = key[index];
        }
    }
    for (usize index = 0usize; index < 64usize; index += 1usize) {
        outer_key[index] = (inner_key[index] ^ 0x5cu8) as u8;
        inner_key[index] = (inner_key[index] ^ 0x36u8) as u8;
    }
    sha256_state inner = sha256_state::create();
    inner.update(inner_key);
    inner.update(message);
    std.hash::sha256_digest inner_digest = inner.finish();
    sha256_state outer = sha256_state::create();
    outer.update(outer_key);
    outer.update(inner_digest.bytes);
    std.secret::zeroize(&inner_key);
    std.secret::zeroize(&outer_key);
    return outer.finish();
}

std.hash::sha512_digest hmac_sha512(const u8[] key, const u8[] message) {
    u8[128] inner_key = {};
    u8[128] outer_key = {};
    if (len(key) > 128usize) {
        std.hash::sha512_digest reduced = std.hash::sha512(key);
        u8[64] reduced_bytes = reduced.bytes;
        for (usize index = 0usize; index < 64usize; index += 1usize) {
            inner_key[index] = reduced_bytes[index];
        }
    } else {
        for (usize index = 0usize; index < len(key); index += 1usize) {
            inner_key[index] = key[index];
        }
    }
    for (usize index = 0usize; index < 128usize; index += 1usize) {
        outer_key[index] = (inner_key[index] ^ 0x5cu8) as u8;
        inner_key[index] = (inner_key[index] ^ 0x36u8) as u8;
    }
    sha512_state inner = sha512_state::create();
    inner.update(inner_key);
    inner.update(message);
    std.hash::sha512_digest inner_digest = inner.finish();
    sha512_state outer = sha512_state::create();
    outer.update(outer_key);
    outer.update(inner_digest.bytes);
    std.secret::zeroize(&inner_key);
    std.secret::zeroize(&outer_key);
    return outer.finish();
}

u8[48] hmac_sha384(const u8[] key, const u8[] message) {
    u8[128] inner_key = {};
    u8[128] outer_key = {};
    if (len(key) > 128usize) {
        u8[48] reduced = sha384(key);
        u8[48] reduced_bytes = reduced;
        for (usize index = 0usize; index < 48usize; index += 1usize) {
            inner_key[index] = reduced_bytes[index];
        }
    } else {
        for (usize index = 0usize; index < len(key); index += 1usize) {
            inner_key[index] = key[index];
        }
    }
    for (usize index = 0usize; index < 128usize; index += 1usize) {
        outer_key[index] = (inner_key[index] ^ 0x5cu8) as u8;
        inner_key[index] = (inner_key[index] ^ 0x36u8) as u8;
    }
    sha384_state inner = sha384_state::create();
    inner.update(inner_key);
    inner.update(message);
    u8[48] inner_digest = inner.finish();
    sha384_state outer = sha384_state::create();
    outer.update(outer_key);
    outer.update(inner_digest[..]);
    std.secret::zeroize(&inner_key);
    std.secret::zeroize(&outer_key);
    return outer.finish();
}
