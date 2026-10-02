module std.encoding;

/* R-SLIB-ENCODING-0001: the symbol of a six-bit value in the base64 alphabet of RFC 4648 section
   4, or in the URL and file name safe alphabet of section 5. */
protected u8 base64_symbol(u32 value, bool url) {
    if (value < 26u32) { return (value + 65u32) as u8; }
    if (value < 52u32) { return (value + 71u32) as u8; }
    if (value < 62u32) { return (value - 4u32) as u8; }
    if (value == 62u32) {
        if (url == true) { return 45u8; }
        return 43u8;
    }
    if (url == true) { return 95u8; }
    return 47u8;
}

/* The six-bit value of a base64 symbol, or 64 for a byte outside the alphabet. */
protected u32 base64_value(u8 symbol, bool url) {
    u32 code = symbol as u32;
    if (code >= 65u32 && code <= 90u32) { return code - 65u32; }
    if (code >= 97u32 && code <= 122u32) { return code - 71u32; }
    if (code >= 48u32 && code <= 57u32) { return code + 4u32; }
    if (url == true) {
        if (code == 45u32) { return 62u32; }
        if (code == 95u32) { return 63u32; }
        return 64u32;
    }
    if (code == 43u32) { return 62u32; }
    if (code == 47u32) { return 63u32; }
    return 64u32;
}

protected std.string::string encode_base64_with(const u8[] data, bool url)
    throws std.alloc::alloc_error {
    usize groups = len(data) / 3usize;
    usize rest = len(data) - groups * 3usize;
    std.string::string result = std.string::with_capacity((groups + 1usize) * 4usize);
    for (usize group = 0usize; group < groups; group += 1usize) {
        usize at = group * 3usize;
        u32 bits = ((data[at] as u32) << 16u32) | ((data[at + 1usize] as u32) << 8u32) |
                   (data[at + 2usize] as u32);
        std.string::push_scalar(&result, base64_symbol(bits >> 18u32, url) as char);
        std.string::push_scalar(&result, base64_symbol((bits >> 12u32) & 63u32, url) as char);
        std.string::push_scalar(&result, base64_symbol((bits >> 6u32) & 63u32, url) as char);
        std.string::push_scalar(&result, base64_symbol(bits & 63u32, url) as char);
    }
    if (rest != 0usize) {
        usize at = groups * 3usize;
        u32 bits = (data[at] as u32) << 16u32;
        if (rest == 2usize) { bits |= (data[at + 1usize] as u32) << 8u32; }
        std.string::push_scalar(&result, base64_symbol(bits >> 18u32, url) as char);
        std.string::push_scalar(&result, base64_symbol((bits >> 12u32) & 63u32, url) as char);
        if (rest == 2usize) {
            std.string::push_scalar(&result, base64_symbol((bits >> 6u32) & 63u32, url) as char);
        }
        if (url == false) {
            if (rest == 1usize) { std.string::append_str(&result, "=="); }
            else { std.string::append_str(&result, "="); }
        }
    }
    return move result;
}

/* R-SLIB-ENCODING-0001: base64 with padding, and base64url without padding. */
std.string::string encode_base64(const u8[] data) throws std.alloc::alloc_error {
    return encode_base64_with(data, false);
}

std.string::string encode_base64_url(const u8[] data) throws std.alloc::alloc_error {
    return encode_base64_with(data, true);
}

protected std.convert::parse_error invalid(usize index) {
    return std.convert::parse_error {.code = std.convert::parse_error_code::invalid_digit,
                                     .index = index};
}

/* R-SLIB-ENCODING-0002: the bytes of a canonical base64 text. The standard alphabet requires
   groups of four symbols with padding; the URL alphabet takes the padding or omits it. The
   checks run in order: the first byte outside the alphabet, a `=` before the padding included,
   reports invalid_digit at its index; an incomplete last group reports trailing_character at
   its first byte; unused bits that are not zero report invalid_digit at the last symbol. */
protected bytes decode_base64_with(str text, bool url)
    throws std.convert::parse_error, std.alloc::alloc_error {
    const u8[] source = text;
    usize total = len(source);
    usize end = total;
    usize padding = 0usize;
    while (end > 0usize && padding < 2usize && source[end - 1usize] == 61u8) {
        end -= 1usize;
        padding += 1usize;
    }
    for (usize index = 0usize; index < end; index += 1usize) {
        throw (base64_value(source[index], url) == 64u32) invalid(index);
    }
    usize rest = end % 4usize;
    throw (rest == 1usize || ((url == false || padding != 0usize) && total % 4usize != 0usize))
        std.convert::parse_error {
            .code = std.convert::parse_error_code::trailing_character, .index = end - rest};
    bytes result = {};
    result.reserve((end / 4usize) * 3usize + 2usize);
    u32 bits = 0u32;
    usize count = 0usize;
    for (usize index = 0usize; index < end; index += 1usize) {
        bits = (bits << 6u32) | base64_value(source[index], url);
        count += 1usize;
        if (count == 4usize) {
            std.bytes::append_u8(&result, (bits >> 16u32) as u8);
            std.bytes::append_u8(&result, ((bits >> 8u32) & 255u32) as u8);
            std.bytes::append_u8(&result, (bits & 255u32) as u8);
            bits = 0u32;
            count = 0usize;
        }
    }
    if (count == 2usize) {
        throw ((bits & 15u32) != 0u32) invalid(end - 1usize);
        std.bytes::append_u8(&result, (bits >> 4u32) as u8);
    }
    if (count == 3usize) {
        throw ((bits & 3u32) != 0u32) invalid(end - 1usize);
        std.bytes::append_u8(&result, (bits >> 10u32) as u8);
        std.bytes::append_u8(&result, ((bits >> 2u32) & 255u32) as u8);
    }
    return move result;
}

bytes decode_base64(str text) throws std.convert::parse_error, std.alloc::alloc_error {
    return decode_base64_with(text, false);
}

bytes decode_base64_url(str text) throws std.convert::parse_error, std.alloc::alloc_error {
    return decode_base64_with(text, true);
}

/* R-SLIB-ENCODING-0003: two lowercase hexadecimal digits per byte, and back from digits of
   either case. The first byte that is not a digit reports invalid_digit at its index; an odd
   count of digits reports trailing_character at the last one. */
protected u8 hex_digit(u32 value) {
    if (value < 10u32) { return (value + 48u32) as u8; }
    return (value + 87u32) as u8;
}

protected u32 hex_value(u8 symbol) {
    u32 code = symbol as u32;
    if (code >= 48u32 && code <= 57u32) { return code - 48u32; }
    if (code >= 97u32 && code <= 102u32) { return code - 87u32; }
    if (code >= 65u32 && code <= 70u32) { return code - 55u32; }
    return 16u32;
}

std.string::string encode_hex(const u8[] data) throws std.alloc::alloc_error {
    std.string::string result = std.string::with_capacity(len(data) * 2usize);
    for (usize index = 0usize; index < len(data); index += 1usize) {
        u32 value = data[index] as u32;
        std.string::push_scalar(&result, hex_digit(value >> 4u32) as char);
        std.string::push_scalar(&result, hex_digit(value & 15u32) as char);
    }
    return move result;
}

bytes decode_hex(str text) throws std.convert::parse_error, std.alloc::alloc_error {
    const u8[] source = text;
    usize total = len(source);
    bytes result = {};
    result.reserve(total / 2usize);
    usize index = 0usize;
    while (index + 1usize < total) {
        u32 high = hex_value(source[index]);
        throw (high == 16u32) invalid(index);
        u32 low = hex_value(source[index + 1usize]);
        throw (low == 16u32) invalid(index + 1usize);
        std.bytes::append_u8(&result, ((high << 4u32) | low) as u8);
        index += 2usize;
    }
    throw (index < total && hex_value(source[index]) == 16u32) invalid(index);
    throw (index < total) std.convert::parse_error {
        .code = std.convert::parse_error_code::trailing_character, .index = index};
    return move result;
}

/* R-SLIB-ENCODING-0004: percent-encoding of RFC 3986: the unreserved bytes A-Z, a-z, 0-9, `-`,
   `.`, `_` and `~` stay, every other byte becomes `%` and two uppercase hexadecimal digits. */
protected bool unreserved(u8 value) {
    u32 code = value as u32;
    return (code >= 65u32 && code <= 90u32) || (code >= 97u32 && code <= 122u32) ||
           (code >= 48u32 && code <= 57u32) || code == 45u32 || code == 46u32 ||
           code == 95u32 || code == 126u32;
}

std.string::string percent_encode(str text) throws std.alloc::alloc_error {
    const u8[] source = text;
    std.string::string result = std.string::with_capacity(len(source));
    for (usize index = 0usize; index < len(source); index += 1usize) {
        u8 value = source[index];
        if (unreserved(value) == true) {
            std.string::push_scalar(&result, value as char);
        } else {
            u32 code = value as u32;
            std.string::push_scalar(&result, '%');
            u32 high = code >> 4u32;
            u32 low = code & 15u32;
            if (high < 10u32) { std.string::push_scalar(&result, (high + 48u32) as char); }
            else { std.string::push_scalar(&result, (high + 55u32) as char); }
            if (low < 10u32) { std.string::push_scalar(&result, (low + 48u32) as char); }
            else { std.string::push_scalar(&result, (low + 55u32) as char); }
        }
    }
    return move result;
}

/* R-SLIB-ENCODING-0004: the bytes of a percent-encoded text: `%` and two hexadecimal digits of
   either case become that byte, every other byte stays. A `%` without two digits reports
   invalid_digit at its index. */
bytes percent_decode(str text) throws std.convert::parse_error, std.alloc::alloc_error {
    const u8[] source = text;
    usize total = len(source);
    bytes result = {};
    result.reserve(total);
    usize index = 0usize;
    while (index < total) {
        u8 value = source[index];
        if (value != 37u8) {
            std.bytes::append_u8(&result, value);
            index += 1usize;
            continue;
        }
        throw (index + 2usize >= total) invalid(index);
        u32 high = hex_value(source[index + 1usize]);
        u32 low = hex_value(source[index + 2usize]);
        throw (high == 16u32 || low == 16u32) invalid(index);
        std.bytes::append_u8(&result, ((high << 4u32) | low) as u8);
        index += 3usize;
    }
    return move result;
}
