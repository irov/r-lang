module test.codegen.library_encoding;

import std.encoding;

// R-SLIB-ENCODING-0001..0004 (M19): base64 and base64url, hexadecimal and percent-encoding, the
// canonical decoding errors with their byte index, and a round trip of every byte value.

protected bool same(str left, str right) { return std.bytes::equal(left, right); }

protected i32 rejects(str text, u32 form, usize index, std.convert::parse_error_code code)
    throws std.alloc::alloc_error {
    try {
        switch (form) {
        case 0u32: std.encoding::decode_base64(text) as void;
        case 1u32: std.encoding::decode_base64_url(text) as void;
        case 2u32: std.encoding::decode_hex(text) as void;
        default: std.encoding::percent_decode(text) as void;
        }
        return 1;
    } catch (std.convert::parse_error failure) {
        if (failure.index != index || failure.code != code) { return 2; }
        return 0;
    }
}

protected i32 check_vectors() throws std.alloc::alloc_error, std.convert::parse_error {
    // RFC 4648 section 10.
    str[7] plain = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    str[7] padded = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    str[7] unpadded = {"", "Zg", "Zm8", "Zm9v", "Zm9vYg", "Zm9vYmE", "Zm9vYmFy"};
    for (usize index = 0usize; index < 7usize; index += 1usize) {
        std.string::string standard = std.encoding::encode_base64(plain[index]);
        if (same(standard.as_str(), padded[index]) == false) { return 1; }
        std.string::string url = std.encoding::encode_base64_url(plain[index]);
        if (same(url.as_str(), unpadded[index]) == false) { return 2; }
        bytes back = std.encoding::decode_base64(padded[index]);
        if (std.bytes::equal(back.as_slice(), plain[index]) == false) { return 3; }
        bytes loose = std.encoding::decode_base64_url(unpadded[index]);
        if (std.bytes::equal(loose.as_slice(), plain[index]) == false) { return 4; }
        bytes strict = std.encoding::decode_base64_url(padded[index]);
        if (std.bytes::equal(strict.as_slice(), plain[index]) == false) { return 5; }
    }
    u8[3] high = {251u8, 255u8, 190u8};
    std.string::string plus = std.encoding::encode_base64(high);
    if (same(plus.as_str(), "+/++") == false) { return 6; }
    std.string::string dash = std.encoding::encode_base64_url(high);
    if (same(dash.as_str(), "-_--") == false) { return 7; }
    std.string::string hex = std.encoding::encode_hex(high);
    if (same(hex.as_str(), "fbffbe") == false) { return 8; }
    bytes unhex = std.encoding::decode_hex("FBffBe");
    if (std.bytes::equal(unhex.as_slice(), high) == false) { return 9; }
    std.string::string pct = std.encoding::percent_encode("a b/é~-._Z9");
    if (same(pct.as_str(), "a%20b%2F%C3%A9~-._Z9") == false) { return 10; }
    bytes unpct = std.encoding::percent_decode("a%20b%2f%C3%a9~+");
    if (std.bytes::equal(unpct.as_slice(), "a b/é~+") == false) { return 11; }
    return 0;
}

protected i32 check_errors() throws std.alloc::alloc_error {
    std.convert::parse_error_code digit = std.convert::parse_error_code::invalid_digit;
    std.convert::parse_error_code trailing = std.convert::parse_error_code::trailing_character;
    if (rejects("Zm9vY", 0u32, 4usize, trailing) != 0) { return 20; }
    if (rejects("Zm9vYg", 0u32, 4usize, trailing) != 0) { return 21; }
    if (rejects("Zm9vYh==", 0u32, 5usize, digit) != 0) { return 22; }
    if (rejects("Zm$v", 0u32, 2usize, digit) != 0) { return 23; }
    if (rejects("Zg=a", 0u32, 2usize, digit) != 0) { return 24; }
    if (rejects("Zm-v", 0u32, 2usize, digit) != 0) { return 25; }
    if (rejects("Zm+v", 1u32, 2usize, digit) != 0) { return 26; }
    if (rejects("Zm9vY", 1u32, 4usize, trailing) != 0) { return 27; }
    if (rejects("Zm9=", 1u32, 2usize, digit) != 0) { return 28; }
    if (rejects("Zm=v", 1u32, 2usize, digit) != 0) { return 35; }
    if (rejects("Zh", 1u32, 1usize, digit) != 0) { return 29; }
    if (rejects("abc", 2u32, 2usize, trailing) != 0) { return 30; }
    if (rejects("abx", 2u32, 2usize, digit) != 0) { return 31; }
    if (rejects("g0", 2u32, 0usize, digit) != 0) { return 32; }
    if (rejects("x%4", 3u32, 1usize, digit) != 0) { return 33; }
    if (rejects("%zz", 3u32, 0usize, digit) != 0) { return 34; }
    return 0;
}

protected i32 check_round_trip() throws std.alloc::alloc_error, std.convert::parse_error {
    u8[256] every = {};
    for (usize index = 0usize; index < 256usize; index += 1usize) { every[index] = index as u8; }
    for (usize length = 0usize; length <= 256usize; length += 37usize) {
        const u8[] part = every[0usize..length];
        std.string::string standard = std.encoding::encode_base64(part);
        bytes back = std.encoding::decode_base64(standard.as_str());
        if (std.bytes::equal(back.as_slice(), part) == false) { return 40; }
        std.string::string url = std.encoding::encode_base64_url(part);
        bytes url_back = std.encoding::decode_base64_url(url.as_str());
        if (std.bytes::equal(url_back.as_slice(), part) == false) { return 41; }
        std.string::string hex = std.encoding::encode_hex(part);
        bytes hex_back = std.encoding::decode_hex(hex.as_str());
        if (std.bytes::equal(hex_back.as_slice(), part) == false) { return 42; }
    }
    return 0;
}

i32 main() {
    try {
        i32 vectors = check_vectors();
        if (vectors != 0) { return vectors; }
        i32 errors = check_errors();
        if (errors != 0) { return errors; }
        return check_round_trip();
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 99;
    } catch (std.convert::parse_error failure) {
        failure as void;
        return 98;
    }
}
