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

protected std.crypto::curve curve_named(str name) {
    switch (name) {
    case "p384": return std.crypto::curve::p384;
    default: return std.crypto::curve::p256;
    }
}

protected std.crypto::rsa_scheme scheme_named(str name) {
    switch (name) {
    case "pkcs1_sha384": return std.crypto::rsa_scheme::pkcs1_sha384;
    case "pkcs1_sha512": return std.crypto::rsa_scheme::pkcs1_sha512;
    case "pss_sha256": return std.crypto::rsa_scheme::pss_sha256;
    case "pss_sha384": return std.crypto::rsa_scheme::pss_sha384;
    case "pss_sha512": return std.crypto::rsa_scheme::pss_sha512;
    default: return std.crypto::rsa_scheme::pkcs1_sha256;
    }
}

/* The PKCS#8 DER of a private key and the SPKI DER of its public key, in hexadecimal. */
protected std.string::string key_pair_hex(const std.crypto::private_key* key)
    throws std.alloc::alloc_error, std.crypto::crypto_error {
    std.secret::buffer private_der = key->to_der();
    std.crypto::public_key public_part = key->public_key();
    bytes public_der = public_part.to_der();
    std.string::string private_hex = hex(std.secret::as_slice(&private_der));
    std.string::string public_hex = hex(public_der.as_slice());
    return f"{private_hex} {public_hex}";
}

/* A signature of `message` by a private key: ECDSA with the hash of its curve, RSA under the
   scheme, Ed25519 as RFC 8032. */
protected bytes sign_with(const std.crypto::private_key* key, std.crypto::rsa_scheme scheme, const u8[] message)
    throws std.alloc::alloc_error, std.crypto::crypto_error {
    switch (*key) {
    case variant std.crypto::private_key::ecdsa(pair): return pair->sign(message);
    case variant std.crypto::private_key::rsa(pair): return pair->sign(scheme, message);
    case variant std.crypto::private_key::ed25519(pair):
        u8[64] signature = pair->sign(message);
        bytes copied = {};
        std.bytes::append(&copied, signature[..]);
        return move copied;
    case variant std.crypto::private_key::x25519(pair):
        pair as void;
        bytes empty = {};
        return move empty;
    }
}

/* Whether `signature` verifies under a public key. */
protected bool verify_with(const std.crypto::public_key* key, std.crypto::rsa_scheme scheme, const u8[] message,
                           const u8[] signature) throws std.crypto::crypto_error {
    switch (*key) {
    case variant std.crypto::public_key::ecdsa(point): return point->verify(message, signature);
    case variant std.crypto::public_key::rsa(modulus): return modulus->verify(scheme, message, signature);
    case variant std.crypto::public_key::ed25519(raw_key): return std.crypto::verify((*raw_key)[..], message, signature);
    case variant std.crypto::public_key::x25519(raw_key):
        raw_key as void;
        return false;
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
    case "ecdsa_public":
        bytes scalar = parse_hex(at[2usize].as_str());
        std.crypto::ecdsa_key key = std.crypto::ecdsa_key::from_scalar(curve_named(at[1usize].as_str()), scalar.as_slice());
        std.crypto::ecdsa_public_key public_part = key.public_key();
        return hex(public_part.point());
    case "ecdsa_sign":
        bytes scalar = parse_hex(at[2usize].as_str());
        bytes message = parse_hex(at[3usize].as_str());
        std.crypto::ecdsa_key key = std.crypto::ecdsa_key::from_scalar(curve_named(at[1usize].as_str()), scalar.as_slice());
        bytes signature = key.sign(message.as_slice());
        return hex(signature.as_slice());
    case "ecdsa_verify":
        bytes point = parse_hex(at[2usize].as_str());
        bytes message = parse_hex(at[3usize].as_str());
        bytes signature = parse_hex(at[4usize].as_str());
        std.crypto::ecdsa_public_key key = std.crypto::ecdsa_public_key::from_point(curve_named(at[1usize].as_str()),
                                                                                    point.as_slice());
        bool valid = key.verify(message.as_slice(), signature.as_slice());
        return f"{valid}";
    case "ecdsa_generate":
        std.crypto::curve which = curve_named(at[1usize].as_str());
        bytes message = parse_hex(at[2usize].as_str());
        std.crypto::ecdsa_key key = std.crypto::ecdsa_key::generate(which);
        bytes signature = key.sign(message.as_slice());
        std.crypto::ecdsa_public_key public_part = key.public_key();
        bool valid = public_part.verify(message.as_slice(), signature.as_slice());
        bool same_curve = public_part.curve() == which && key.curve() == which;
        usize sizes = which.scalar_length() + which.point_length() + which.signature_length();
        std.crypto::private_key wrapped = std.crypto::private_key::ecdsa(move key);
        std.string::string pair = key_pair_hex(&wrapped);
        std.string::string signature_hex = hex(signature.as_slice());
        return f"{pair} {signature_hex} {valid} {same_curve} {sizes}";
    case "rsa_generate":
        bytes message = parse_hex(at[3usize].as_str());
        std.crypto::rsa_key key = std.crypto::rsa_key::generate(parse_number(at[1usize].as_str()));
        std.crypto::rsa_scheme scheme = scheme_named(at[2usize].as_str());
        bytes signature = key.sign(scheme, message.as_slice());
        std.crypto::rsa_public_key public_part = key.public_key();
        bool valid = public_part.verify(scheme, message.as_slice(), signature.as_slice());
        usize bits = key.bits() + public_part.bits();
        std.crypto::private_key wrapped = std.crypto::private_key::rsa(move key);
        std.string::string pair = key_pair_hex(&wrapped);
        std.string::string signature_hex = hex(signature.as_slice());
        return f"{pair} {signature_hex} {valid} {bits}";
    case "rsa_components":
        bytes modulus = parse_hex(at[1usize].as_str());
        bytes exponent = parse_hex(at[2usize].as_str());
        std.crypto::rsa_public_key key = std.crypto::rsa_public_key::from_components(modulus.as_slice(),
                                                                                     exponent.as_slice());
        std.string::string modulus_hex = hex(key.modulus());
        std.string::string exponent_hex = hex(key.exponent());
        usize bits = key.bits();
        return f"{modulus_hex} {exponent_hex} {bits}";
    case "sign_der":
        bytes der = parse_hex(at[1usize].as_str());
        bytes message = parse_hex(at[3usize].as_str());
        std.crypto::private_key key = std.crypto::private_key::from_der(der.as_slice());
        bytes signature = sign_with(&key, scheme_named(at[2usize].as_str()), message.as_slice());
        return hex(signature.as_slice());
    case "verify_der":
        bytes der = parse_hex(at[1usize].as_str());
        bytes message = parse_hex(at[3usize].as_str());
        bytes signature = parse_hex(at[4usize].as_str());
        std.crypto::public_key key = std.crypto::public_key::from_der(der.as_slice());
        bool valid = verify_with(&key, scheme_named(at[2usize].as_str()), message.as_slice(), signature.as_slice());
        return f"{valid}";
    case "private_der":
        bytes der = parse_hex(at[1usize].as_str());
        std.crypto::private_key key = std.crypto::private_key::from_der(der.as_slice());
        return key_pair_hex(&key);
    case "private_pem":
        bytes text = parse_hex(at[1usize].as_str());
        std.crypto::private_key key = std.crypto::private_key::from_pem(text.as_slice());
        std.secret::buffer pem = key.to_pem();
        std.string::string pair = key_pair_hex(&key);
        std.string::string pem_hex = hex(std.secret::as_slice(&pem));
        return f"{pair} {pem_hex}";
    case "public_der":
        bytes der = parse_hex(at[1usize].as_str());
        std.crypto::public_key key = std.crypto::public_key::from_der(der.as_slice());
        bytes out = key.to_der();
        return hex(out.as_slice());
    case "public_pem":
        bytes text = parse_hex(at[1usize].as_str());
        std.crypto::public_key key = std.crypto::public_key::from_pem(text.as_slice());
        bytes out = key.to_der();
        std.string::string pem = key.to_pem();
        std.string::string der_hex = hex(out.as_slice());
        std.string::string pem_hex = hex(pem.as_bytes());
        return f"{der_hex} {pem_hex}";
    case "cbc_encrypt":
        bytes key = parse_hex(at[1usize].as_str());
        bytes iv = parse_hex(at[2usize].as_str());
        bytes plaintext = parse_hex(at[3usize].as_str());
        bytes sealed = std.crypto::cbc_encrypt(key.as_slice(), iv.as_slice(), plaintext.as_slice());
        return hex(sealed.as_slice());
    case "cbc_decrypt":
        bytes key = parse_hex(at[1usize].as_str());
        bytes iv = parse_hex(at[2usize].as_str());
        bytes ciphertext = parse_hex(at[3usize].as_str());
        bytes opened = std.crypto::cbc_decrypt(key.as_slice(), iv.as_slice(), ciphertext.as_slice());
        return hex(opened.as_slice());
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
