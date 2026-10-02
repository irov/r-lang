module std.uuid;
import std.cmp;
import std.random;

/* R-SLIB-UUID-0001: an identifier of RFC 9562 in the byte order of the RFC. Identifiers compare
   by their bytes, so identifiers of version 7 sort by their time. */
@derive(equal, ordered)
struct uuid {
    u8[16] bytes;
};

/* The dictionary key operations of Core R-FUNC-0009: equal bytes and a rotate-and-xor hash of
   them. */
u64 uuid::hash(const uuid* value) {
    u64 result = 0u64;
    for (usize index = 0usize; index < 16usize; index += 1usize) {
        result = ((result << 7u64) | (result >> 57u64)) ^ (value->bytes[index] as u64);
    }
    return result;
}

bool uuid::equal(const uuid* left, const uuid* right) {
    for (usize index = 0usize; index < 16usize; index += 1usize) {
        if (left->bytes[index] != right->bytes[index]) { return false; }
    }
    return true;
}

protected u8 digit(u8 value) {
    if (value < 10u8) { return (value + 48u8) as u8; }
    return (value + 87u8) as u8;
}

/* The 36 lowercase characters xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx. */
impl core::Format for uuid {
    void format(const uuid* this, std.format::builder* out) throws std.alloc::alloc_error {
        u8[36] text = {};
        usize at = 0usize;
        for (usize index = 0usize; index < 16usize; index += 1usize) {
            if (index == 4usize || index == 6usize || index == 8usize || index == 10usize) {
                text[at] = 45u8;
                at += 1usize;
            }
            u8 value = this->bytes[index];
            text[at] = digit((value >> 4u8) as u8);
            text[at + 1usize] = digit((value & 15u8) as u8);
            at += 2usize;
        }
        try {
            std.format::append_str(out, core::validate_utf8(text));
        } catch (core::utf8_error failure) {
            failure as void;
        }
    }
};


/* R-SLIB-UUID-0001: the Nil and Max identifiers of RFC 9562 section 5.9 and 5.10. */
uuid nil() {
    return uuid {.bytes = {}};
}

uuid max() {
    u8[16] bytes = {};
    for (usize index = 0usize; index < 16usize; index += 1usize) { bytes[index] = 255u8; }
    return uuid {.bytes = bytes};
}

/* The version: the upper four bits of byte 6. */
u8 version(const uuid* value) {
    return (value->bytes[6] >> 4u8) as u8;
}

/* R-SLIB-UUID-0002: version 4 from 16 random bytes: the version and variant bits replace six
   of them. */
uuid from_random_v4(u8[16] random) {
    u8[16] bytes = random;
    bytes[6] = ((bytes[6] & 15u8) | 64u8) as u8;
    bytes[8] = ((bytes[8] & 63u8) | 128u8) as u8;
    return uuid {.bytes = bytes};
}

/* R-SLIB-UUID-0002: version 7 from a Unix time in milliseconds, of which the lower 48 bits are
   kept, and 10 random bytes: the low four bits of the first, the second, the low six bits of the
   third and the remaining seven. */
uuid from_time_v7(u64 unix_milliseconds, u8[10] random) {
    u8[16] bytes = {};
    for (usize index = 0usize; index < 6usize; index += 1usize) {
        u64 shift = (40usize - index * 8usize) as u64;
        bytes[index] = ((unix_milliseconds >> shift) & 255u64) as u8;
    }
    bytes[6] = (112u8 | (random[0] & 15u8)) as u8;
    bytes[7] = random[1];
    bytes[8] = (128u8 | (random[2] & 63u8)) as u8;
    for (usize index = 3usize; index < 10usize; index += 1usize) {
        bytes[index + 6usize] = random[index];
    }
    return uuid {.bytes = bytes};
}

uuid v4() {
    u8[16] random = {};
    std.random::fill(&random);
    return from_random_v4(random);
}

/* R-SLIB-UUID-0002: the time of the system clock; before 1970 or beyond the 48-bit range it
   reports overflow. */
uuid v7() throws std.time::time_error {
    std.time::system_time now = std.time::system_now();
    throw (now.unix_seconds < 0i64 || now.unix_seconds >= 281474976710i64)
        std.time::time_error {.code = std.time::error_code::overflow, .native_code = 0i64};
    u64 milliseconds = (now.unix_seconds as u64) * 1000u64 + (now.nanoseconds / 1000000u32) as u64;
    u8[10] random = {};
    std.random::fill(&random);
    return from_time_v7(milliseconds, random);
}

protected u32 hex_value(u8 symbol) {
    u32 code = symbol as u32;
    if (code >= 48u32 && code <= 57u32) { return code - 48u32; }
    if (code >= 97u32 && code <= 102u32) { return code - 87u32; }
    if (code >= 65u32 && code <= 70u32) { return code - 55u32; }
    return 16u32;
}

/* R-SLIB-UUID-0003: the 36-character form with hyphens after 8, 12, 16 and 20 digits, digits of
   either case. */
uuid parse(str text) throws std.convert::parse_error {
    const u8[] source = text;
    usize total = len(source);
    throw (total == 0usize)
        std.convert::parse_error {.code = std.convert::parse_error_code::empty, .index = 0usize};
    u8[16] bytes = {};
    usize digits = 0usize;
    usize limit = total;
    if (limit > 36usize) { limit = 36usize; }
    for (usize index = 0usize; index < limit; index += 1usize) {
        u8 symbol = source[index];
        bool hyphen = index == 8usize || index == 13usize || index == 18usize || index == 23usize;
        if (hyphen == true) {
            throw (symbol != 45u8) std.convert::parse_error {
                .code = std.convert::parse_error_code::invalid_digit, .index = index};
        } else {
            u32 value = hex_value(symbol);
            throw (value == 16u32) std.convert::parse_error {
                .code = std.convert::parse_error_code::invalid_digit, .index = index};
            usize slot = digits / 2usize;
            if (digits % 2usize == 0usize) {
                bytes[slot] = (value << 4u32) as u8;
            } else {
                bytes[slot] = (bytes[slot] | (value as u8)) as u8;
            }
            digits += 1usize;
        }
    }
    throw (total < 36usize) std.convert::parse_error {
        .code = std.convert::parse_error_code::invalid_digit, .index = total};
    throw (total > 36usize) std.convert::parse_error {
        .code = std.convert::parse_error_code::trailing_character, .index = 36usize};
    return uuid {.bytes = bytes};
}
