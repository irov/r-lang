module tests.std.crypto;
import std.test;
import std.crypto;
import std.encoding;

// The tests of std.crypto (Library R-SLIB-CRYPTO-0001..0008), run in test mode (Core R-FUNC-0025).

protected std.string::string hex(const u8[] data) throws std.alloc::alloc_error {
    return std.encoding::encode_hex(data);
}

@test
void signs_the_rfc_8032_message() throws std.test::failure, std.alloc::alloc_error, std.crypto::crypto_error,
    std.convert::parse_error {
    bytes seed = std.encoding::decode_hex("4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb");
    std.crypto::signing_key key = std.crypto::signing_key::from_seed(seed.as_slice());
    std.string::string public_hex = hex(key.public_key[..]);
    std.test::equal_text(public_hex.as_str(), "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c");
    u8[1] message = {0x72u8};
    u8[64] signature = key.sign(message[..]);
    std.string::string signature_hex = hex(signature[..]);
    std.test::equal_text(signature_hex.as_str(),
                         "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                         "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00");
    std.test::check(std.crypto::verify(key.public_key[..], message[..], signature[..]), "verifies");
    std.test::check(std.crypto::verify(key.public_key[..], "x", signature[..]) == false, "other message");
}

@test
void seals_and_opens() throws std.test::failure, std.alloc::alloc_error, std.crypto::crypto_error {
    bytes key = std.crypto::random(32usize);
    bytes nonce = std.crypto::random(std.crypto::aead::xchacha20_poly1305.nonce_length());
    bytes sealed = std.crypto::seal(std.crypto::aead::xchacha20_poly1305, key.as_slice(), nonce.as_slice(),
                                    "header", "secret");
    std.test::equal(len(sealed), 6usize + std.crypto::aead::xchacha20_poly1305.tag_length());
    bytes opened = std.crypto::open(std.crypto::aead::xchacha20_poly1305, key.as_slice(), nonce.as_slice(),
                                    "header", sealed.as_slice());
    std.test::check(std.bytes::equal(opened.as_slice(), "secret"), "opened");
    try {
        bytes forged = std.crypto::open(std.crypto::aead::xchacha20_poly1305, key.as_slice(), nonce.as_slice(),
                                        "other", sealed.as_slice());
        drop forged;
        std.test::fail("other aad opened");
    } catch (std.crypto::crypto_error failure) {
        std.test::check(failure.code == std.crypto::error_code::authentication_failed, "refused");
    }
}

@test
void agrees_on_a_secret() throws std.test::failure, std.alloc::alloc_error, std.crypto::crypto_error {
    std.crypto::exchange_key alice = std.crypto::exchange_key::generate();
    std.crypto::exchange_key bob = std.crypto::exchange_key::generate();
    std.secret::buffer one = alice.shared(bob.public_key[..]);
    std.secret::buffer two = bob.shared(alice.public_key[..]);
    std.test::check(std.secret::constant_time_equal(std.secret::as_slice(&one), std.secret::as_slice(&two)),
                    "same secret");
    bytes derived = std.crypto::hkdf_sha512("salt", std.secret::as_slice(&one), "info", 64usize);
    std.test::equal(len(derived), 64usize);
}

@test
void hashes_passwords_and_data() throws std.test::failure, std.alloc::alloc_error, std.crypto::crypto_error {
    std.crypto::password_limits limits = std.crypto::password_limits {.operations = 1u64, .memory = 8192usize};
    std.string::string hash = std.crypto::password_hash("hunter2", limits);
    std.test::check(std.crypto::password_verify(hash.as_str(), "hunter2"), "matches");
    std.test::check(std.crypto::password_verify(hash.as_str(), "hunter3") == false, "other password");
    std.test::check(std.crypto::password_limits::interactive().memory == 67108864usize, "interactive");
    bytes digest = std.crypto::blake2b("abc", "", 32usize);
    std.crypto::blake2b_state state = std.crypto::blake2b_state::create("", 32usize);
    state.update("a");
    state.update("bc");
    bytes pieces = state.finish();
    std.test::check(std.bytes::equal(digest.as_slice(), pieces.as_slice()), "same digest");
    bytes stretched = std.crypto::argon2id("pw", "0123456789abcdef", limits, 16usize);
    std.test::equal(len(stretched), 16usize);
}

// R-SLIB-CRYPTO-0009, R-SLIB-CRYPTO-0012: the RFC 6979 key of appendix A.2.5 signs "sample"
// deterministically, and a key survives PKCS#8 PEM and SPKI DER.
@test
void signs_with_ecdsa_and_keeps_the_key_in_pem() throws std.test::failure, std.alloc::alloc_error,
    std.crypto::crypto_error, std.convert::parse_error {
    bytes scalar = std.encoding::decode_hex("c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721");
    std.crypto::ecdsa_key key = std.crypto::ecdsa_key::from_scalar(std.crypto::curve::p256, scalar.as_slice());
    bytes signature = key.sign("sample");
    std.string::string signature_hex = hex(signature.as_slice());
    std.test::equal_text(signature_hex.as_str(),
                         "efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716"
                         "f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8");
    std.crypto::private_key wrapped = std.crypto::private_key::ecdsa(move key);
    std.secret::buffer pem = wrapped.to_pem();
    std.crypto::private_key again = std.crypto::private_key::from_pem(std.secret::as_slice(&pem));
    std.crypto::public_key public_part = again.public_key();
    bytes der = public_part.to_der();
    std.crypto::public_key read = std.crypto::public_key::from_der(der.as_slice());
    switch (read) {
    case variant std.crypto::public_key::ecdsa(point):
        std.test::check(point->verify("sample", signature.as_slice()), "verifies after PEM and DER");
        std.test::check(point->verify("other", signature.as_slice()) == false, "other message");
    default: std.test::fail("an ECDSA key");
    }
}

// R-SLIB-CRYPTO-0010: a new RSA key signs under PKCS#1 v1.5 and PSS, and its public key verifies.
@test
void signs_with_rsa() throws std.test::failure, std.alloc::alloc_error, std.crypto::crypto_error {
    std.crypto::rsa_key key = std.crypto::rsa_key::generate(2048usize);
    std.test::equal(key.bits(), 2048usize);
    std.crypto::rsa_public_key public_part = key.public_key();
    bytes plain = key.sign(std.crypto::rsa_scheme::pkcs1_sha256, "token");
    bytes probabilistic = key.sign(std.crypto::rsa_scheme::pss_sha256, "token");
    std.test::equal(len(plain), 256usize);
    std.test::check(public_part.verify(std.crypto::rsa_scheme::pkcs1_sha256, "token", plain.as_slice()), "PKCS#1");
    std.test::check(public_part.verify(std.crypto::rsa_scheme::pss_sha256, "token", probabilistic.as_slice()), "PSS");
    std.test::check(public_part.verify(std.crypto::rsa_scheme::pss_sha256, "token", plain.as_slice()) == false,
                    "scheme mismatch");
    std.crypto::rsa_public_key rebuilt = std.crypto::rsa_public_key::from_components(public_part.modulus(),
                                                                                     public_part.exponent());
    std.test::check(rebuilt.verify(std.crypto::rsa_scheme::pkcs1_sha256, "token", plain.as_slice()), "components");
}

// R-SLIB-CRYPTO-0011: AES-CBC pads to whole blocks and refuses a padding that does not check.
@test
void encrypts_with_cbc() throws std.test::failure, std.alloc::alloc_error, std.crypto::crypto_error {
    bytes key = std.crypto::random(16usize);
    bytes iv = std.crypto::random(16usize);
    bytes sealed = std.crypto::cbc_encrypt(key.as_slice(), iv.as_slice(), "sixteen bytes!!!");
    std.test::equal(len(sealed), 32usize);
    bytes opened = std.crypto::cbc_decrypt(key.as_slice(), iv.as_slice(), sealed.as_slice());
    std.test::check(std.bytes::equal(opened.as_slice(), "sixteen bytes!!!"), "round trip");
    try {
        bytes truncated = std.crypto::cbc_decrypt(key.as_slice(), iv.as_slice(), "short");
        drop truncated;
        std.test::fail("a short ciphertext opened");
    } catch (std.crypto::crypto_error failure) {
        std.test::check(failure.code == std.crypto::error_code::invalid_length, "invalid length");
    }
}
