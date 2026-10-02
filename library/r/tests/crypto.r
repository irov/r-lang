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
