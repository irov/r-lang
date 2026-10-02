module std.random;
import std.slice;

/* R-SLIB-RANDOM-0001: the R part of std.random, loaded by `import std.random;`. Its functions
   draw bytes from std.random::fill, the generator of the operating system. */

/* The little-endian value of up to eight bytes. */
protected u64 little_endian(const u8[] bytes) {
    u64 value = 0u64;
    usize index = len(bytes);
    while (index > 0usize) {
        index -= 1usize;
        value = (value << 8u64) | (bytes[index] as u64);
    }
    return value;
}

u64 next_u64() {
    u8[8] bytes = {};
    std.random::fill(&bytes);
    return little_endian(bytes);
}

u32 next_u32() {
    u8[4] bytes = {};
    std.random::fill(&bytes);
    return little_endian(bytes) as u32;
}

/* (2^64 - bound) mod bound: values below it are rejected so that every remainder is equally
   likely. A zero bound overflows the addition and panics as Core arithmetic does. */
protected u64 rejection_limit(u64 bound) {
    return (18446744073709551615u64 - bound + 1u64) % bound;
}

u64 below(u64 bound) {
    u64 limit = rejection_limit(bound);
    while (true) {
        u64 value = next_u64();
        if (value >= limit) { return value % bound; }
    }
    return 0u64;
}

/* An empty range panics: low above high overflows the subtraction, low equal to high makes
   the bound zero. */
u64 range(u64 low, u64 high) {
    return low + below(high - low);
}

/* R-SLIB-RANDOM-0002: xoshiro256** 1.0 over four state words. */
struct generator {
    protected u64[4] state;
};

protected u64 rotate_left(u64 value, u64 count) {
    return (value << count) | (value >> (64u64 - count));
}

/* One output of SplitMix64: it advances the running value first. */
protected u64 split_mix(u64* running) {
    *running = core::wrapping_add_u64(*running, 0x9e3779b97f4a7c15u64);
    u64 x = *running;
    u64 y = core::wrapping_mul_u64(x ^ (x >> 30u64), 0xbf58476d1ce4e5b9u64);
    u64 z = core::wrapping_mul_u64(y ^ (y >> 27u64), 0x94d049bb133111ebu64);
    return z ^ (z >> 31u64);
}

generator generator::seeded(u64 seed) {
    u64 running = seed;
    u64 s0 = split_mix(&running);
    u64 s1 = split_mix(&running);
    u64 s2 = split_mix(&running);
    u64 s3 = split_mix(&running);
    return generator {.state = {s0, s1, s2, s3}};
}

generator generator::from_entropy() {
    u8[32] bytes = {};
    while (true) {
        std.random::fill(&bytes);
        u64 s0 = little_endian(bytes[0usize..8usize]);
        u64 s1 = little_endian(bytes[8usize..16usize]);
        u64 s2 = little_endian(bytes[16usize..24usize]);
        u64 s3 = little_endian(bytes[24usize..32usize]);
        if ((s0 | s1 | s2 | s3) != 0u64) { return generator {.state = {s0, s1, s2, s3}}; }
    }
    return generator::seeded(0u64);
}

u64 generator::next_u64(generator* this) {
    u64 s0 = this->state[0];
    u64 s1 = this->state[1];
    u64 s2 = this->state[2];
    u64 s3 = this->state[3];
    u64 result =
        core::wrapping_mul_u64(rotate_left(core::wrapping_mul_u64(s1, 5u64), 7u64), 9u64);
    u64 shifted = s1 << 17u64;
    u64 t2 = s2 ^ s0;
    u64 t3 = s3 ^ s1;
    this->state[1] = s1 ^ t2;
    this->state[0] = s0 ^ t3;
    this->state[2] = t2 ^ shifted;
    this->state[3] = rotate_left(t3, 45u64);
    return result;
}

u32 generator::next_u32(generator* this) {
    return (this->next_u64() >> 32u64) as u32;
}

u64 generator::below(generator* this, u64 bound) {
    u64 limit = rejection_limit(bound);
    while (true) {
        u64 value = this->next_u64();
        if (value >= limit) { return value % bound; }
    }
    return 0u64;
}

u64 generator::range(generator* this, u64 low, u64 high) {
    return low + this->below(high - low);
}

void generator::fill(generator* this, u8[] target) {
    usize at = 0usize;
    usize total = len(target);
    while (at < total) {
        u64 value = this->next_u64();
        u64 rest = value;
        usize take = total - at;
        if (take > 8usize) { take = 8usize; }
        for (usize index = 0usize; index < take; index += 1usize) {
            target[at + index] = (rest & 255u64) as u8;
            rest = rest >> 8u64;
        }
        at += take;
    }
}

/* Fisher–Yates from the last element: each position takes a uniformly chosen one of the
   elements up to it. */
@generic<T>
void generator::shuffle(generator* this, T[] items) {
    usize index = len(items);
    while (index > 1usize) {
        index -= 1usize;
        usize other = this->below((index + 1usize) as u64) as usize;
        std.slice::swap(items, index, other);
    }
}
