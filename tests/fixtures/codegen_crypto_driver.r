module test.codegen.crypto_driver;
import std.crypto;
import std.console;

/* Runs one std.crypto operation per input line, `OPERATION ARG...` with hexadecimal arguments
   (`-` for an empty one) and decimal numbers, and prints its hexadecimal result or the error. */
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

protected usize parse_number(str text) {
    const u8[] digits = text;
    usize number = 0usize;
    for (usize index = 0usize; index < len(digits); index += 1usize) {
        number = number * 10usize + ((digits[index] - 48u8) as usize);
    }
    return number;
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

/* The words of a line. */
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

protected std.crypto::aead algorithm(str name) {
    switch (name) {
    case "xchacha20_poly1305": return std.crypto::aead::xchacha20_poly1305;
    default: return std.crypto::aead::chacha20_poly1305;
    }
}

protected std.string::string run(const array<std.string::string>* arguments)
    throws std.alloc::alloc_error, std.crypto::crypto_error {
    const std.string::string[] at = std.array::as_slice(arguments);
    switch (at[0usize].as_str()) {
    case "sign":
        bytes seed = parse_hex(at[1usize].as_str());
        bytes message = parse_hex(at[2usize].as_str());
        std.crypto::signing_key key = std.crypto::signing_key::from_seed(seed.as_slice());
        u8[64] signature = key.sign(message.as_slice());
        std.string::string public_hex = hex(key.public_key[..]);
        std.string::string signature_hex = hex(signature[..]);
        return f"{public_hex} {signature_hex}";
    case "verify":
        bytes public_key = parse_hex(at[1usize].as_str());
        bytes message = parse_hex(at[2usize].as_str());
        bytes signature = parse_hex(at[3usize].as_str());
        bool valid = std.crypto::verify(public_key.as_slice(), message.as_slice(), signature.as_slice());
        return f"{valid}";
    case "seal":
        bytes key = parse_hex(at[2usize].as_str());
        bytes nonce = parse_hex(at[3usize].as_str());
        bytes aad = parse_hex(at[4usize].as_str());
        bytes plaintext = parse_hex(at[5usize].as_str());
        bytes sealed = std.crypto::seal(algorithm(at[1usize].as_str()), key.as_slice(), nonce.as_slice(),
                                        aad.as_slice(), plaintext.as_slice());
        return hex(sealed.as_slice());
    case "open":
        bytes key = parse_hex(at[2usize].as_str());
        bytes nonce = parse_hex(at[3usize].as_str());
        bytes aad = parse_hex(at[4usize].as_str());
        bytes ciphertext = parse_hex(at[5usize].as_str());
        bytes opened = std.crypto::open(algorithm(at[1usize].as_str()), key.as_slice(), nonce.as_slice(),
                                        aad.as_slice(), ciphertext.as_slice());
        return hex(opened.as_slice());
    case "x25519":
        bytes secret = parse_hex(at[1usize].as_str());
        bytes peer = parse_hex(at[2usize].as_str());
        std.crypto::exchange_key key = std.crypto::exchange_key::from_secret(secret.as_slice());
        std.secret::buffer shared = key.shared(peer.as_slice());
        std.string::string public_hex = hex(key.public_key[..]);
        std.string::string shared_hex = hex(std.secret::as_slice(&shared));
        return f"{public_hex} {shared_hex}";
    case "hkdf256":
        bytes salt = parse_hex(at[1usize].as_str());
        bytes material = parse_hex(at[2usize].as_str());
        bytes info = parse_hex(at[3usize].as_str());
        bytes derived = std.crypto::hkdf_sha256(salt.as_slice(), material.as_slice(), info.as_slice(),
                                                parse_number(at[4usize].as_str()));
        return hex(derived.as_slice());
    case "hkdf512":
        bytes salt = parse_hex(at[1usize].as_str());
        bytes material = parse_hex(at[2usize].as_str());
        bytes info = parse_hex(at[3usize].as_str());
        bytes derived = std.crypto::hkdf_sha512(salt.as_slice(), material.as_slice(), info.as_slice(),
                                                parse_number(at[4usize].as_str()));
        return hex(derived.as_slice());
    case "blake2b":
        bytes data = parse_hex(at[1usize].as_str());
        bytes key = parse_hex(at[2usize].as_str());
        bytes digest = std.crypto::blake2b(data.as_slice(), key.as_slice(), parse_number(at[3usize].as_str()));
        return hex(digest.as_slice());
    case "blake2b_stream":
        bytes data = parse_hex(at[1usize].as_str());
        bytes key = parse_hex(at[2usize].as_str());
        std.crypto::blake2b_state state = std.crypto::blake2b_state::create(key.as_slice(),
                                                                            parse_number(at[3usize].as_str()));
        const u8[] all = data.as_slice();
        const usize half = len(all) / 2usize;
        state.update(all[0usize..half]);
        state.update(all[half..len(all)]);
        bytes digest = state.finish();
        return hex(digest.as_slice());
    case "argon2id":
        bytes password = parse_hex(at[1usize].as_str());
        bytes salt = parse_hex(at[2usize].as_str());
        std.crypto::password_limits limits = std.crypto::password_limits {
            .operations = parse_number(at[3usize].as_str()) as u64, .memory = parse_number(at[4usize].as_str()),
        };
        bytes derived = std.crypto::argon2id(password.as_slice(), salt.as_slice(), limits,
                                             parse_number(at[5usize].as_str()));
        return hex(derived.as_slice());
    case "password":
        bytes password = parse_hex(at[1usize].as_str());
        std.crypto::password_limits limits = std.crypto::password_limits {.operations = 1u64, .memory = 8192usize};
        std.string::string hash = std.crypto::password_hash(password.as_slice(), limits);
        bool good = std.crypto::password_verify(hash.as_str(), password.as_slice());
        bool bad = std.crypto::password_verify(hash.as_str(), "wrong");
        return f"{good} {bad}";
    default: return std.string::from_str("unknown");
    }
}

async i32 main() {
    while (true) {
        o<std.string::string> line = await std.console::read_line();
        switch (move line) {
        case variant o::some(move text):
            try {
                array<std.string::string> arguments = words(text.as_str());
                await std.console::println(run(&arguments));
            } catch (std.crypto::crypto_error failure) {
                std.crypto::error_code code = failure.code;
                await std.console::println(f"error {code}");
            } catch (std.array::push_error<std.string::string> failure) { return 71; }
        case variant o::none: return 0;
        }
    }
    return 0;
}
