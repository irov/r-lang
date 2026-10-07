module test.codegen.cose_driver;
import std.cbor;
import std.cose;
import std.crypto;
import std.console;

/* Runs one std.cose operation per input line with hexadecimal arguments (`-` for empty) and
   prints the hexadecimal result or the error. Header buckets are given as encoded CBOR maps
   with integer labels. */
protected u8 nibble(u8 digit) {
    if (digit >= 97u8) { return (digit - 87u8) as u8; }
    if (digit >= 65u8) { return (digit - 55u8) as u8; }
    return (digit - 48u8) as u8;
}

protected bytes parse_hex(str text) throws std.alloc::alloc_error {
    const u8[] digits = text;
    bytes data = {};
    if (len(digits) == 1usize) { return move data; }
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
    if (len(data) == 0usize) { std.string::push_scalar(&text, '-'); }
    return move text;
}

protected array<std.string::string> words(str line) throws std.alloc::alloc_error, std.array::push_error<std.string::string> {
    array<std.string::string> found = [];
    const u8[] raw_bytes = line;
    usize start = 0usize;
    for (usize index = 0usize; index <= len(raw_bytes); index += 1usize) {
        if (index == len(raw_bytes) || raw_bytes[index] == 32u8) {
            if (index > start) {
                try {
                    found.push(std.string::from_str(core::validate_utf8(raw_bytes[start..index])));
                } catch (core::utf8_error rejected) { rejected as void; }
            }
            start = index + 1usize;
        }
    }
    return move found;
}

/* The integer label of a header key. */
protected i64 label_of(const std.cbor::value* key) {
    switch (*key) {
    case variant std.cbor::value::unsigned(number): return *number as i64;
    case variant std.cbor::value::negative(number): return (-1i64) - (*number as i64);
    default: return 0i64;
    }
}

/* Adds the entries of an encoded map to a bucket of `fields`. */
protected void add_headers(std.cose::headers* fields, str encoded, bool protect)
    throws std.alloc::alloc_error, std.cbor::cbor_error {
    bytes data = parse_hex(encoded);
    if (len(data) == 0usize) { return; }
    std.cbor::value decoded = std.cbor::decode(data.as_slice());
    switch (move decoded) {
    case variant std.cbor::value::map(move entries):
        array<std.cbor::entry> pending = move entries;
        array<std.cbor::entry> ordered = [];
        while (len(pending) > 0usize) {
            o<std.cbor::entry> popped = std.array::pop(&pending);
            switch (move popped) {
            case variant o::some(move pair):
                try { ordered.push(move pair); } catch (std.array::push_error<std.cbor::entry> rejected) { drop rejected; }
            case variant o::none: break;
            }
        }
        while (len(ordered) > 0usize) {
            o<std.cbor::entry> popped = std.array::pop(&ordered);
            switch (move popped) {
            case variant o::some(move pair):
                // The entry is taken apart by a pattern test (Core R-STMT-0002).
                if (move pair is {.key = move key, .item = move item}) {
                    const i64 label = label_of(&key);
                    drop key;
                    if (protect == true) {
                        fields->protect(label, move item);
                    } else {
                        fields->expose(label, move item);
                    }
                }
            case variant o::none: break;
            }
        }
    default: break;
    }
}

protected std.string::string payload_hex(const std.cose::message* received) throws std.alloc::alloc_error {
    switch (received->content) {
    case variant o::some(data): return hex(std.array::as_slice(data));
    case variant o::none: return std.string::from_str("detached");
    }
}

protected std.string::string run(const array<std.string::string>* arguments)
    throws std.alloc::alloc_error, std.crypto::crypto_error, std.cose::cose_error, std.cbor::cbor_error {
    const std.string::string[] at = std.array::as_slice(arguments);
    switch (at[0usize]) {
    case "sign1":
        bytes seed = parse_hex(at[1usize]);
        std.cose::headers fields = std.cose::headers::create();
        add_headers(&fields, at[2usize], true);
        add_headers(&fields, at[3usize], false);
        bytes payload = parse_hex(at[4usize]);
        bytes external = parse_hex(at[5usize]);
        std.crypto::signing_key key = std.crypto::signing_key::from_seed(seed.as_slice());
        bytes message = std.cose::sign1(move fields, payload.as_slice(), &key, external.as_slice());
        return hex(message.as_slice());
    case "verify_sign1":
        bytes public_key = parse_hex(at[1usize]);
        bytes external = parse_hex(at[2usize]);
        bytes data = parse_hex(at[3usize]);
        std.cose::message received = std.cose::verify_sign1(data.as_slice(), public_key.as_slice(), external.as_slice());
        return payload_hex(&received);
    case "mac0":
        bytes key = parse_hex(at[1usize]);
        std.cose::headers fields = std.cose::headers::create();
        add_headers(&fields, at[2usize], true);
        add_headers(&fields, at[3usize], false);
        bytes payload = parse_hex(at[4usize]);
        bytes external = parse_hex(at[5usize]);
        bytes message = std.cose::mac0(move fields, payload.as_slice(), key.as_slice(), external.as_slice());
        return hex(message.as_slice());
    case "verify_mac0":
        bytes key = parse_hex(at[1usize]);
        bytes external = parse_hex(at[2usize]);
        bytes data = parse_hex(at[3usize]);
        std.cose::message received = std.cose::verify_mac0(data.as_slice(), key.as_slice(), external.as_slice());
        return payload_hex(&received);
    case "encrypt0":
        bytes key = parse_hex(at[1usize]);
        bytes iv = parse_hex(at[2usize]);
        std.cose::headers fields = std.cose::headers::create();
        add_headers(&fields, at[3usize], true);
        add_headers(&fields, at[4usize], false);
        bytes plaintext = parse_hex(at[5usize]);
        bytes external = parse_hex(at[6usize]);
        bytes message = std.cose::encrypt0(move fields, plaintext.as_slice(), key.as_slice(), iv.as_slice(),
                                           external.as_slice());
        return hex(message.as_slice());
    case "decrypt0":
        bytes key = parse_hex(at[1usize]);
        bytes external = parse_hex(at[2usize]);
        bytes data = parse_hex(at[3usize]);
        bytes plaintext = std.cose::decrypt0(data.as_slice(), key.as_slice(), external.as_slice());
        return hex(plaintext.as_slice());
    default: return std.string::from_str("unknown");
    }
}

async i32 main() {
    while (true) {
        o<std.string::string> line = await std.console::read_line();
        switch (move line) {
        case variant o::some(move text):
            try {
                array<std.string::string> arguments = words(text);
                await std.console::println(run(&arguments));
            } catch (std.cose::cose_error failure) {
                std.cose::error_code code = failure.code;
                await std.console::println(f"error {code}");
            } catch (std.crypto::crypto_error failure) {
                std.crypto::error_code code = failure.code;
                await std.console::println(f"error {code}");
            } catch (std.cbor::cbor_error failure) {
                std.cbor::error_code code = failure.code;
                await std.console::println(f"error {code}");
            } catch (std.array::push_error<std.string::string> failure) { return 71; }
        case variant o::none: return 0;
        }
    }
    return 0;
}
