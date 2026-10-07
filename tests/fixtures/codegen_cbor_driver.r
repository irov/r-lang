module test.codegen.cbor_driver;
import std.cbor;
import std.console;

/* Reads hexadecimal CBOR items, one per line, and prints for each its diagnostic notation, its
   deterministic encoding and whether it already was deterministic, or the decoding error. */
protected u8 nibble(u8 digit) {
    if (digit >= 97u8) { return (digit - 87u8) as u8; }
    if (digit >= 65u8) { return (digit - 55u8) as u8; }
    return (digit - 48u8) as u8;
}

protected bytes parse_hex(str text) throws std.alloc::alloc_error {
    const u8[] digits = text;
    bytes data = {};
    for (usize index = 0usize; index + 1usize < len(digits); index += 2usize) {
        std.bytes::append_u8(&data, ((nibble(digits[index]) * 16u8) + nibble(digits[index + 1usize])) as u8);
    }
    return move data;
}

protected u8 hex_digit(u8 number) {
    if (number < 10u8) { return (number + 48u8) as u8; }
    return (number + 87u8) as u8;
}

protected std.string::string hex(const u8[] data) throws std.alloc::alloc_error {
    std.string::string text = std.string::create();
    for (usize index = 0usize; index < len(data); index += 1usize) {
        std.string::push_scalar(&text, hex_digit((data[index] >> 4u8) as u8) as char);
        std.string::push_scalar(&text, hex_digit((data[index] & 15u8) as u8) as char);
    }
    return move text;
}

protected std.string::string report(const u8[] input) throws std.alloc::alloc_error {
    try {
        std.cbor::value item = std.cbor::decode(input);
        std.string::string shown = std.cbor::diagnostic(&item);
        bytes encoded = std.cbor::encode(&item);
        std.string::string encoded_hex = hex(encoded.as_slice());
        str strict = "deterministic";
        try {
            std.cbor::value again = std.cbor::decode_deterministic(input);
            drop again;
        } catch (std.cbor::cbor_error failure) {
            strict = "not-deterministic";
        }
        return f"{shown}\t{encoded_hex}\t{strict}";
    } catch (std.cbor::cbor_error failure) {
        std.cbor::error_code code = failure.code;
        usize offset = failure.offset;
        return f"error\t{code}\t{offset}";
    }
}

async i32 main() {
    while (true) {
        o<std.string::string> line = await std.console::read_line();
        switch (move line) {
        case variant o::some(move text):
            bytes input = parse_hex(text);
            await std.console::println(report(input.as_slice()));
        case variant o::none: return 0;
        }
    }
    return 0;
}
