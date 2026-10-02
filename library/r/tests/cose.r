module tests.std.cose;
import std.test;
import std.cbor;
import std.crypto;
import std.cose;

// The tests of std.cose (Library R-SLIB-COSE-0001..0006), run in test mode (Core R-FUNC-0025).

@test
void signs_and_verifies() throws std.test::failure, std.alloc::alloc_error, std.crypto::crypto_error,
    std.cose::cose_error {
    std.crypto::signing_key key = std.crypto::signing_key::generate();
    std.cose::headers fields = std.cose::headers::create();
    fields.expose(std.cose::KEY_ID, std.cbor::value::of_bytes("k1"));
    bytes message = std.cose::sign1(move fields, "payload", &key, "context");
    std.cose::message checked = std.cose::verify_sign1(message.as_slice(), key.public_key[..], "context");
    std.test::equal(checked.tag, std.cose::SIGN1);
    switch (checked.fields.algorithm()) {
    case variant o::some(algorithm): std.test::check(*algorithm == std.cose::EDDSA, "EdDSA");
    case variant o::none: std.test::fail("no algorithm");
    }
    try {
        std.cose::message other = std.cose::verify_sign1(message.as_slice(), key.public_key[..], "other");
        drop other;
        std.test::fail("other context verified");
    } catch (std.cose::cose_error failure) {
        std.test::check(failure.code == std.cose::error_code::verification_failed, "refused");
    }
}

@test
void tags_with_every_hmac() throws std.test::failure, std.alloc::alloc_error, std.cose::cose_error {
    i64[4] algorithms = {std.cose::HMAC_256_64, std.cose::HMAC_256, std.cose::HMAC_384, std.cose::HMAC_512};
    usize[4] lengths = {8usize, 32usize, 48usize, 64usize};
    for (usize index = 0usize; index < 4usize; index += 1usize) {
        std.cose::headers fields = std.cose::headers::create();
        fields.protect(std.cose::ALGORITHM, std.cbor::value::integer(algorithms[index]));
        bytes message = std.cose::mac0(move fields, "data", "a key of some length", "");
        std.cose::message checked = std.cose::verify_mac0(message.as_slice(), "a key of some length", "");
        std.test::equal(len(checked.last), lengths[index]);
    }
}

@test
void encrypts_and_decrypts() throws std.test::failure, std.alloc::alloc_error, std.crypto::crypto_error,
    std.cose::cose_error {
    bytes key = std.crypto::random(32usize);
    bytes iv = std.crypto::random(12usize);
    bytes message = std.cose::encrypt0(std.cose::headers::create(), "plain", key.as_slice(), iv.as_slice(), "");
    std.cose::message read = std.cose::decode(message.as_slice(), std.cose::ENCRYPT0);
    switch (read.fields.find(std.cose::IV)) {
    case variant o::some(found): found as void;
    case variant o::none: std.test::fail("no iv");
    }
    bytes plain = std.cose::decrypt0(message.as_slice(), key.as_slice(), "");
    std.test::check(std.bytes::equal(plain.as_slice(), "plain"), "plaintext");
    try {
        std.cose::message wrong = std.cose::decode(message.as_slice(), std.cose::MAC0);
        drop wrong;
        std.test::fail("wrong type read");
    } catch (std.cose::cose_error failure) {
        std.test::check(failure.code == std.cose::error_code::wrong_type, "wrong type");
    }
}
