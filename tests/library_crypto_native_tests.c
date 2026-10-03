/* The native provider of std.crypto (Library R-SLIB-CRYPTO-0001..0008): each primitive against a
   known answer of its RFC, the refusals the R part relies on, and the BLAKE2b state kept in
   caller memory without alignment. */
#include "r_std_crypto_native.h"

#include <stdio.h>
#include <string.h>

static int r_crypto_failures;

#define R_CRYPTO_CHECK(condition)                                                                  \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            ++r_crypto_failures;                                                                   \
        }                                                                                          \
    } while (0)

static size_t r_crypto_unhex(const char *text, uint8_t *target, size_t capacity) {
    size_t length = 0U;
    while ((text[0] != '\0') && (text[1] != '\0') && (length < capacity)) {
        unsigned int byte = 0U;
        (void)sscanf(text, "%2x", &byte);
        target[length++] = (uint8_t)byte;
        text += 2;
    }
    return length;
}

static int r_crypto_equal_hex(const uint8_t *data, size_t length, const char *text) {
    uint8_t expected[256];
    const size_t expected_length = r_crypto_unhex(text, expected, sizeof(expected));
    return (expected_length == length) && (memcmp(data, expected, length) == 0);
}

static void r_crypto_test_ed25519(void) {
    uint8_t seed[32];
    uint8_t public_key[32];
    uint8_t secret_key[64];
    uint8_t signature[64];
    const uint8_t message[1] = {0x72U};

    (void)r_crypto_unhex(
        "4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb", seed, sizeof(seed));
    R_CRYPTO_CHECK(r_std_crypto_native_sign_seed_keypair(seed, public_key, secret_key) == 0);
    R_CRYPTO_CHECK(r_crypto_equal_hex(
        public_key, 32U, "3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c"));
    R_CRYPTO_CHECK(r_std_crypto_native_sign(signature, message, 1U, secret_key) == 0);
    R_CRYPTO_CHECK(
        r_crypto_equal_hex(signature,
                           64U,
                           "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da"
                           "085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"));
    R_CRYPTO_CHECK(r_std_crypto_native_verify(signature, message, 1U, public_key) == 0);
    signature[0] ^= 1U;
    R_CRYPTO_CHECK(r_std_crypto_native_verify(signature, message, 1U, public_key) == -1);
}

static void r_crypto_test_exchange(void) {
    uint8_t alice[32];
    uint8_t bob_public[32];
    uint8_t shared[32];
    uint8_t zero[32] = {0};

    (void)r_crypto_unhex(
        "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a", alice, sizeof(alice));
    (void)r_crypto_unhex("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f",
                         bob_public,
                         sizeof(bob_public));
    R_CRYPTO_CHECK(r_std_crypto_native_exchange_shared(shared, alice, bob_public) == 0);
    R_CRYPTO_CHECK(r_crypto_equal_hex(
        shared, 32U, "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742"));
    R_CRYPTO_CHECK(r_std_crypto_native_exchange_shared(shared, alice, zero) == -1);
}

static void r_crypto_test_aead(void) {
    uint8_t key[32];
    uint8_t nonce[12];
    uint8_t sealed[64];
    uint8_t opened[48];
    const uint8_t message[] = "sealed in memory";
    const size_t length = sizeof(message) - 1U;

    memset(key, 7, sizeof(key));
    memset(nonce, 9, sizeof(nonce));
    R_CRYPTO_CHECK(
        r_std_crypto_native_aead_seal(0, key, nonce, NULL, 0U, message, length, sealed) == 0);
    R_CRYPTO_CHECK(
        r_std_crypto_native_aead_open(0, key, nonce, NULL, 0U, sealed, length + 16U, opened) == 0);
    R_CRYPTO_CHECK(memcmp(opened, message, length) == 0);
    sealed[length] ^= 1U;
    R_CRYPTO_CHECK(
        r_std_crypto_native_aead_open(0, key, nonce, NULL, 0U, sealed, length + 16U, opened) == -1);
    R_CRYPTO_CHECK(r_std_crypto_native_aead_open(0, key, nonce, NULL, 0U, sealed, 15U, opened) ==
                   -1);
}

static void r_crypto_test_hkdf_and_blake2b(void) {
    uint8_t salt[13];
    uint8_t material[22];
    uint8_t info[10];
    uint8_t derived[42];
    uint8_t digest[64];
    uint8_t state[R_STD_CRYPTO_NATIVE_BLAKE2B_STATE_BYTES + 1U];
    const uint8_t abc[3] = {'a', 'b', 'c'};

    (void)r_crypto_unhex("000102030405060708090a0b0c", salt, sizeof(salt));
    memset(material, 0x0b, sizeof(material));
    (void)r_crypto_unhex("f0f1f2f3f4f5f6f7f8f9", info, sizeof(info));
    R_CRYPTO_CHECK(r_std_crypto_native_hkdf(256,
                                            salt,
                                            sizeof(salt),
                                            material,
                                            sizeof(material),
                                            info,
                                            sizeof(info),
                                            derived,
                                            sizeof(derived)) == 0);
    R_CRYPTO_CHECK(r_crypto_equal_hex(
        derived,
        sizeof(derived),
        "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b887185865"));
    R_CRYPTO_CHECK(
        r_std_crypto_native_hkdf(
            256, NULL, 0U, material, sizeof(material), NULL, 0U, derived, 255U * 32U + 1U) == -1);
    R_CRYPTO_CHECK(r_std_crypto_native_blake2b(digest, 64U, abc, 3U, NULL, 0U) == 0);
    R_CRYPTO_CHECK(
        r_crypto_equal_hex(digest,
                           64U,
                           "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
                           "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"));
    /* An odd address: the provider copies the state to aligned storage for every call. */
    R_CRYPTO_CHECK(r_std_crypto_native_blake2b_start(state + 1, NULL, 0U, 64U) == 0);
    R_CRYPTO_CHECK(r_std_crypto_native_blake2b_update(state + 1, abc, 1U) == 0);
    R_CRYPTO_CHECK(r_std_crypto_native_blake2b_update(state + 1, abc + 1, 2U) == 0);
    memset(digest, 0, sizeof(digest));
    R_CRYPTO_CHECK(r_std_crypto_native_blake2b_finish(state + 1, digest, 64U) == 0);
    R_CRYPTO_CHECK(
        r_crypto_equal_hex(digest,
                           64U,
                           "ba80a53f981c4d0d6a2797b69f12f6e94c212f14685ac4b74b12bb6fdbffa2d1"
                           "7d87c5392aab792dc252d5de4533cc9518d38aa8dbf1925ab92386edd4009923"));
    R_CRYPTO_CHECK(r_std_crypto_native_blake2b(digest, 8U, abc, 3U, NULL, 0U) == -1);
}

static void r_crypto_test_passwords(void) {
    uint8_t salt[16];
    uint8_t derived[32];
    uint8_t hash[R_STD_CRYPTO_NATIVE_PASSWORD_HASH_BYTES];
    const uint8_t password[] = "pass";

    memset(salt, 1, sizeof(salt));
    R_CRYPTO_CHECK(
        r_std_crypto_native_argon2id(derived, sizeof(derived), password, 4U, salt, 1U, 8192U) == 0);
    R_CRYPTO_CHECK(r_std_crypto_native_argon2id(
                       derived, sizeof(derived), password, 4U, salt, 0U, 8192U) == -1);
    R_CRYPTO_CHECK(r_std_crypto_native_password_hash(hash, password, 4U, 1U, 8192U) == 0);
    R_CRYPTO_CHECK(memcmp(hash, "$argon2id$", 10U) == 0);
    R_CRYPTO_CHECK(
        r_std_crypto_native_password_verify(hash, strlen((const char *)hash), password, 4U) == 0);
    R_CRYPTO_CHECK(
        r_std_crypto_native_password_verify(hash, strlen((const char *)hash), password, 3U) == -1);
}

int main(void) {
    uint8_t random[32] = {0};
    R_CRYPTO_CHECK(r_std_crypto_native_ready() == 0);
    R_CRYPTO_CHECK(r_std_crypto_native_random(random, sizeof(random)) == 0);
    r_crypto_test_ed25519();
    r_crypto_test_exchange();
    r_crypto_test_aead();
    r_crypto_test_hkdf_and_blake2b();
    r_crypto_test_passwords();
    if (r_crypto_failures != 0) {
        (void)fprintf(stderr, "%d crypto native checks failed\n", r_crypto_failures);
        return 1;
    }
    (void)puts("r_library_crypto_native_tests: ok");
    return 0;
}
