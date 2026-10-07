module test.codegen.jwt_driver;
import std.console;
import std.crypto;
import std.encoding;
import std.jwt;

/* Runs one std.jwt operation per input line and prints its result or `error CODE`:
     sign ALG KEY CLAIMS          a token; KEY is the HMAC secret or a PKCS#8 DER key
     verify ALG KEY TOKEN NOW     the claims; KEY is the secret or an SPKI DER key
     jwks SET TOKEN NOW           the claims, the keys from a JWK Set
     jwk ALG KEY KID              the JWK of an SPKI DER key
   KEY, CLAIMS and SET are hexadecimal, NOW is a decimal count of seconds since 1970. */

protected bytes hex_of(str text) throws std.alloc::alloc_error {
    try {
        return std.encoding::decode_hex(text);
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    }
    bytes empty = {};
    return move empty;
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

protected std.time::system_time at(str seconds) {
    try {
        i64 value = std.convert::parse_i64(seconds, 10u32);
        return std.time::system_time {.unix_seconds = value, .nanoseconds = 0u32};
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    }
    return std.time::system_time {.unix_seconds = 0i64, .nanoseconds = 0u32};
}

protected bool is_hmac(std.jwt::algorithm alg) {
    return alg == std.jwt::algorithm::hs256 || alg == std.jwt::algorithm::hs384 || alg == std.jwt::algorithm::hs512;
}

protected std.jwt::validation rules() {
    std.jwt::validation result = std.jwt::validation::create();
    result.require_expiration = false;
    return move result;
}

protected std.string::string run(const array<std.string::string>* arguments)
    throws std.alloc::alloc_error, std.jwt::jwt_error, std.crypto::crypto_error, std.json::error {
    const std.string::string[] at_words = std.array::as_slice(arguments);
    switch (at_words[0usize]) {
    case "sign":
        std.jwt::algorithm alg = std.jwt::algorithm::parse(at_words[1usize]);
        bytes key = hex_of(at_words[2usize]);
        bytes claims_text = hex_of(at_words[3usize]);
        std.json::value claims = std.json::parse(claims_text.as_slice());
        if (is_hmac(alg) == true) {
            std.jwt::signer hmac = std.jwt::signer::hmac(alg, key.as_slice());
            return std.jwt::sign(&hmac, &claims);
        }
        std.crypto::private_key private_part = std.crypto::private_key::from_der(key.as_slice());
        std.jwt::signer signer = std.jwt::signer::with_private_key(alg, move private_part);
        return std.jwt::sign(&signer, &claims);
    case "verify":
        std.jwt::algorithm alg = std.jwt::algorithm::parse(at_words[1usize]);
        bytes key = hex_of(at_words[2usize]);
        std.jwt::key_set keys = std.jwt::key_set::create();
        if (is_hmac(alg) == true) {
            keys.add(std.jwt::verifier::hmac(alg, key.as_slice()));
        } else {
            std.crypto::public_key public_part = std.crypto::public_key::from_der(key.as_slice());
            keys.add(std.jwt::verifier::with_public_key(alg, move public_part));
        }
        std.jwt::validation checks = rules();
        std.json::value claims = std.jwt::verify(&keys, at_words[3usize], &checks, at(at_words[4usize]));
        return std.json::stringify(&claims);
    case "jwks":
        bytes set = hex_of(at_words[1usize]);
        std.jwt::key_set keys = std.jwt::key_set::from_jwks(set.as_slice());
        std.jwt::validation checks = rules();
        std.json::value claims = std.jwt::verify(&keys, at_words[2usize], &checks, at(at_words[3usize]));
        return std.json::stringify(&claims);
    case "jwk":
        std.jwt::algorithm alg = std.jwt::algorithm::parse(at_words[1usize]);
        bytes key = hex_of(at_words[2usize]);
        std.crypto::public_key public_part = std.crypto::public_key::from_der(key.as_slice());
        std.json::value jwk = std.jwt::jwk(&public_part, alg, at_words[3usize]);
        return std.json::stringify(&jwk);
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
            } catch (std.jwt::jwt_error failure) {
                std.jwt::error_code code = failure.code;
                await std.console::println(f"error {code}");
            } catch (std.crypto::crypto_error failure) {
                std.crypto::error_code code = failure.code;
                await std.console::println(f"error crypto {code}");
            } catch (std.json::error failure) {
                (move failure) as void;
                await std.console::println(std.string::from_str("error json"));
            } catch (std.array::push_error<std.string::string> failure) { return 71; }
        case variant o::none: return 0;
        }
    }
    return 0;
}
