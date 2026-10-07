module test.codegen.encoding_driver;

import std.console;
import std.encoding;
import std.text;

// The arguments are pairs of a mode and its input; the program prints one line per pair
// (M19 differential test, tests/encoding_differential.py). The modes b64, b64url and hex take
// decimal byte values separated by spaces and print the text; pct takes a text and prints its
// percent-encoding; d64, d64url, dhex and dpct take a text and print "bytes" followed by the
// decimal byte values, or "error", the code and the index.

protected bool same(str left, str right) { return std.bytes::equal(left, right); }

protected bytes numbers(str values) throws std.alloc::alloc_error {
    bytes result = {};
    for (str part in std.text::split(values, " ")) {
        const u8[] digits = part;
        u32 value = 0u32;
        for (usize index = 0usize; index < len(digits); index += 1usize) {
            value = value * 10u32 + ((digits[index] as u32) - 48u32);
        }
        if (len(digits) != 0usize) { std.bytes::append_u8(&result, value as u8); }
    }
    return move result;
}

protected void listing(std.string::string* out, const u8[] data) throws std.alloc::alloc_error {
    std.string::append_str(out, "bytes");
    for (usize index = 0usize; index < len(data); index += 1usize) {
        u32 value = data[index] as u32;
        std.string::string piece = f" {value}";
        std.string::append_str(out, piece);
    }
}

protected bytes decode(str mode, str text)
    throws std.convert::parse_error, std.alloc::alloc_error {
    if (same(mode, "d64") == true) { return std.encoding::decode_base64(text); }
    if (same(mode, "d64url") == true) { return std.encoding::decode_base64_url(text); }
    if (same(mode, "dhex") == true) { return std.encoding::decode_hex(text); }
    return std.encoding::percent_decode(text);
}

protected std.string::string encode(str mode, const u8[] data) throws std.alloc::alloc_error {
    if (same(mode, "b64") == true) { return std.encoding::encode_base64(data); }
    if (same(mode, "b64url") == true) { return std.encoding::encode_base64_url(data); }
    return std.encoding::encode_hex(data);
}

protected void decoded(std.string::string* out, str mode, str text) throws std.alloc::alloc_error {
    try {
        bytes result = decode(mode, text);
        listing(out, result.as_slice());
    } catch (std.convert::parse_error failure) {
        u32 code = 0u32;
        if (failure.code == std.convert::parse_error_code::trailing_character) { code = 1u32; }
        usize index = failure.index;
        std.string::string piece = f"error {code} {index}";
        std.string::append_str(out, piece);
    }
}

async i32 main(const str[] arguments) {
    try {
        std.string::string out = std.string::create();
        for (usize at = 1usize; at + 1usize < len(arguments); at += 2usize) {
            str mode = arguments[at];
            str input = arguments[at + 1usize];
            if (same(mode, "b64") == true || same(mode, "b64url") == true ||
                same(mode, "hex") == true) {
                bytes data = numbers(input);
                std.string::string text = encode(mode, data.as_slice());
                std.string::append_str(&out, text);
            } else {
                if (same(mode, "pct") == true) {
                    std.string::string text = std.encoding::percent_encode(input);
                    std.string::append_str(&out, text);
                } else {
                    decoded(&out, mode, input);
                }
            }
            std.string::append_str(&out, "\n");
        }
        await std.console::print(move out);
        return 0;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 99;
    } catch (std.error::fault failure) {
        failure as void;
        return 98;
    }
}
