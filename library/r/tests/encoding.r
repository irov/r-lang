module tests.std.encoding;
import std.test;
import std.encoding;

// The tests of std.encoding (Library R-SLIB-ENCODING-0001..0004), run in test mode
// (Core R-FUNC-0025).

/* The decoders of the module. */
protected enum decoder { base64, base64_url, hex, percent };

/* Fails unless decoding the text reports the code at the index. */
protected void expect_error(str text, decoder form, std.convert::parse_error_code code,
                            usize index) throws std.test::failure, std.alloc::alloc_error {
    try {
        switch (form) {
        case decoder::base64: std.encoding::decode_base64(text) as void;
        case decoder::base64_url: std.encoding::decode_base64_url(text) as void;
        case decoder::hex: std.encoding::decode_hex(text) as void;
        case decoder::percent: std.encoding::percent_decode(text) as void;
        }
        std.string::string message = f"\"{text}\" decoded";
        std.test::fail(message);
    } catch (std.convert::parse_error failure) {
        std.string::string message = f"the error code of \"{text}\"";
        std.test::check(failure.code == code, message);
        std.test::equal(failure.index, index);
    }
}

@test
void encodes_base64_vectors() throws std.test::failure, std.alloc::alloc_error {
    // RFC 4648 section 10: the standard alphabet pads, the URL alphabet does not.
    str[7] plain = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    str[7] padded = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    str[7] unpadded = {"", "Zg", "Zm8", "Zm9v", "Zm9vYg", "Zm9vYmE", "Zm9vYmFy"};
    for (usize index = 0usize; index < 7usize; index += 1usize) {
        std.string::string standard = std.encoding::encode_base64(plain[index]);
        std.test::equal_text(standard, padded[index]);
        std.string::string url = std.encoding::encode_base64_url(plain[index]);
        std.test::equal_text(url, unpadded[index]);
    }
    // Symbols 62 and 63 differ between the alphabets.
    u8[6] high = {0x00u8, 0x7fu8, 0x80u8, 0xffu8, 0x3eu8, 0x3fu8};
    std.string::string standard = std.encoding::encode_base64(high);
    std.test::equal_text(standard, "AH+A/z4/");
    std.string::string url = std.encoding::encode_base64_url(high);
    std.test::equal_text(url, "AH-A_z4_");
    std.string::string scalars = std.encoding::encode_base64("é🙂");
    std.test::equal_text(scalars, "w6nwn5mC");
}

@test
void decodes_base64_in_both_alphabets()
    throws std.test::failure, std.alloc::alloc_error, std.convert::parse_error {
    str[7] plain = {"", "f", "fo", "foo", "foob", "fooba", "foobar"};
    str[7] padded = {"", "Zg==", "Zm8=", "Zm9v", "Zm9vYg==", "Zm9vYmE=", "Zm9vYmFy"};
    str[7] unpadded = {"", "Zg", "Zm8", "Zm9v", "Zm9vYg", "Zm9vYmE", "Zm9vYmFy"};
    for (usize index = 0usize; index < 7usize; index += 1usize) {
        bytes standard = std.encoding::decode_base64(padded[index]);
        std.test::check(std.bytes::equal(standard.as_slice(), plain[index]) == true,
                        "base64 decoding");
        // The URL alphabet takes the padding or omits it.
        bytes bare = std.encoding::decode_base64_url(unpadded[index]);
        std.test::check(std.bytes::equal(bare.as_slice(), plain[index]) == true,
                        "base64url without padding");
        bytes with_padding = std.encoding::decode_base64_url(padded[index]);
        std.test::check(std.bytes::equal(with_padding.as_slice(), plain[index]) == true,
                        "base64url with padding");
    }
    u8[6] high = {0x00u8, 0x7fu8, 0x80u8, 0xffu8, 0x3eu8, 0x3fu8};
    bytes from_standard = std.encoding::decode_base64("AH+A/z4/");
    std.test::check(std.bytes::equal(from_standard.as_slice(), high) == true, "+ and /");
    bytes from_url = std.encoding::decode_base64_url("AH-A_z4_");
    std.test::check(std.bytes::equal(from_url.as_slice(), high) == true, "- and _");
    std.test::equal(len(from_url), 6usize);
}

@test
void encodes_and_decodes_hex()
    throws std.test::failure, std.alloc::alloc_error, std.convert::parse_error {
    u8[5] values = {0x00u8, 0x09u8, 0x7fu8, 0xabu8, 0xffu8};
    std.string::string digits = std.encoding::encode_hex(values);
    std.test::equal_text(digits, "00097fabff");
    // Digits of either case decode.
    bytes decoded = std.encoding::decode_hex("00097FaBfF");
    std.test::check(std.bytes::equal(decoded.as_slice(), values) == true, "mixed case digits");
    bytes empty = std.encoding::decode_hex("");
    std.test::equal(len(empty), 0usize);
    std.string::string none = std.encoding::encode_hex(empty.as_slice());
    std.test::equal_text(none, "");
}

@test
void percent_encodes_text()
    throws std.test::failure, std.alloc::alloc_error, std.convert::parse_error {
    // The unreserved bytes stay; every other byte becomes % and two uppercase digits.
    std.string::string encoded = std.encoding::percent_encode("a b/é~-._Z9?&=");
    std.test::equal_text(encoded, "a%20b%2F%C3%A9~-._Z9%3F%26%3D");
    bytes decoded = std.encoding::percent_decode(encoded);
    std.test::check(std.bytes::equal(decoded.as_slice(), "a b/é~-._Z9?&=") == true,
                    "a round trip");
    // Digits of either case decode; a plus sign stays a plus sign.
    bytes mixed = std.encoding::percent_decode("a%2fb%2F%c3%A9+c");
    std.test::check(std.bytes::equal(mixed.as_slice(), "a/b/é+c") == true, "mixed case digits");
}

@test(expect = std.convert::parse_error)
void rejects_an_odd_count_of_hex_digits()
    throws std.convert::parse_error, std.alloc::alloc_error {
    bytes decoded = std.encoding::decode_hex("abc");
    drop decoded;
}

@test
void reports_the_first_invalid_byte() throws std.test::failure, std.alloc::alloc_error {
    std.convert::parse_error_code digit = std.convert::parse_error_code::invalid_digit;
    std.convert::parse_error_code trailing = std.convert::parse_error_code::trailing_character;
    // A byte outside the alphabet or an early `=`; an incomplete last group; unused bits.
    expect_error("Zm$v", decoder::base64, digit, 2usize);
    expect_error("Zg=a", decoder::base64, digit, 2usize);
    expect_error("Zm-v", decoder::base64, digit, 2usize);
    expect_error("Zm9vY", decoder::base64, trailing, 4usize);
    expect_error("Zm9vYg", decoder::base64, trailing, 4usize);
    expect_error("Zm9vYh==", decoder::base64, digit, 5usize);
    expect_error("Zm+v", decoder::base64_url, digit, 2usize);
    expect_error("Zm9vY", decoder::base64_url, trailing, 4usize);
    expect_error("Zh", decoder::base64_url, digit, 1usize);
    expect_error("g0", decoder::hex, digit, 0usize);
    expect_error("abx", decoder::hex, digit, 2usize);
    expect_error("abc", decoder::hex, trailing, 2usize);
    expect_error("%zz", decoder::percent, digit, 0usize);
    expect_error("x%4", decoder::percent, digit, 1usize);
}

@test(allocations)
void round_trips_every_byte_value()
    throws std.test::failure, std.alloc::alloc_error, std.convert::parse_error {
    u8[256] every = {};
    for (usize index = 0usize; index < 256usize; index += 1usize) { every[index] = index as u8; }
    std.string::string standard = std.encoding::encode_base64(every);
    std.test::equal(standard.len(), 344usize);
    std.test::contains(standard, "8PHy8/T19vf4+fr7/P3+/w==");
    bytes standard_back = std.encoding::decode_base64(standard);
    std.test::check(std.bytes::equal(standard_back.as_slice(), every) == true, "base64");
    std.string::string url = std.encoding::encode_base64_url(every);
    std.test::equal(url.len(), 342usize);
    std.test::contains(url, "8PHy8_T19vf4-fr7_P3-_w");
    bytes url_back = std.encoding::decode_base64_url(url);
    std.test::check(std.bytes::equal(url_back.as_slice(), every) == true, "base64url");
    std.string::string digits = std.encoding::encode_hex(every);
    std.test::equal(digits.len(), 512usize);
    bytes digits_back = std.encoding::decode_hex(digits);
    std.test::check(std.bytes::equal(digits_back.as_slice(), every) == true, "hex");
}
