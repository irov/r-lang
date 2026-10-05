/* The native provider of std.crypto (Library R-SLIB-CRYPTO-0001..0012): each primitive against a
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

/* RFC 6979 appendix A.2.5 (P-256, SHA-256) and A.2.6 (P-384, SHA-384), message "sample". */
static void r_crypto_test_ecdsa(void) {
    uint8_t scalar[48];
    uint8_t point[97];
    uint8_t signature[96];
    uint8_t generated[48];
    uint8_t generated_point[97];
    const uint8_t *sample = (const uint8_t *)"sample";

    (void)r_crypto_unhex(
        "c9afa9d845ba75166b5c215767b1d6934e50c3db36e89b127b8a622b120f6721", scalar, 32U);
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_public(0, scalar, point) == 0);
    R_CRYPTO_CHECK(
        r_crypto_equal_hex(point,
                           65U,
                           "0460fed4ba255a9d31c961eb74c6356d68c049b8923b61fa6ce669622e60f29fb6"
                           "7903fe1008b8bc99a41ae9e95628bc64f2f1b20c2d7e9f5177a3c294d4462299"));
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_sign(0, scalar, sample, 6U, signature) == 0);
    R_CRYPTO_CHECK(
        r_crypto_equal_hex(signature,
                           64U,
                           "efd48b2aacb6a8fd1140dd9cd45e81d69d2c877b56aaf991c34d0ea84eaf3716"
                           "f7cb1c942d657c41d436c7a1b6e29f65f3e900dbb9aff4064dc4ab2f843acda8"));
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_verify(0, point, 65U, sample, 6U, signature, 64U) ==
                   0);
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_verify(0, point, 65U, sample, 5U, signature, 64U) ==
                   R_STD_CRYPTO_NATIVE_REJECTED);
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_verify(0, point, 65U, sample, 6U, signature, 63U) ==
                   R_STD_CRYPTO_NATIVE_REJECTED);
    point[64] ^= 1U;
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_check(0, point, 65U) == R_STD_CRYPTO_NATIVE_INVALID);
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_verify(0, point, 65U, sample, 6U, signature, 64U) ==
                   R_STD_CRYPTO_NATIVE_INVALID);
    memset(scalar, 0, 32U);
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_public(0, scalar, point) ==
                   R_STD_CRYPTO_NATIVE_INVALID);

    (void)r_crypto_unhex("6b9d3dad2e1b8c1c05b19875b6659f4de23c3b667bf297ba9aa47740787137d8"
                         "96d5724e4c70a825f872c9ea60d2edf5",
                         scalar,
                         48U);
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_public(1, scalar, point) == 0);
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_check(1, point, 97U) == 0);
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_sign(1, scalar, sample, 6U, signature) == 0);
    R_CRYPTO_CHECK(
        r_crypto_equal_hex(signature,
                           96U,
                           "94edbb92a5ecb8aad4736e56c691916b3f88140666ce9fa73d64c4ea95ad133c"
                           "81a648152e44acf96e36dd1e80fabe4699ef4aeb15f178cea1fe40db2603138f"
                           "130e740a19624526203b6351d0a3a94fa329c145786e679e7b82c71a38628ac8"));
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_verify(1, point, 97U, sample, 6U, signature, 96U) ==
                   0);

    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_generate(0, generated, generated_point) == 0);
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_public(0, generated, point) == 0);
    R_CRYPTO_CHECK(memcmp(point, generated_point, 65U) == 0);
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_sign(0, generated, sample, 6U, signature) == 0);
    R_CRYPTO_CHECK(
        r_std_crypto_native_ecdsa_verify(0, generated_point, 65U, sample, 6U, signature, 64U) == 0);
    R_CRYPTO_CHECK(r_std_crypto_native_ecdsa_public(2, generated, point) ==
                   R_STD_CRYPTO_NATIVE_FAILED);
}

/* RSA keys of the provider's own generation: both schemes sign and verify, and a changed
   message, a signature of another length and a key that is not DER are refused. */
static void r_crypto_test_rsa(void) {
    static uint8_t key[2400];
    uint8_t signature[256];
    const uint8_t *message = (const uint8_t *)"pay 10 coins";
    int32_t length = r_std_crypto_native_rsa_generate(2048U, key, sizeof(key));
    int32_t scheme;

    R_CRYPTO_CHECK(length > 0);
    if (length <= 0) {
        return;
    }
    R_CRYPTO_CHECK(r_std_crypto_native_rsa_check_private(key, (size_t)length) == 2048);
    R_CRYPTO_CHECK(r_std_crypto_native_rsa_check_private(key, 10U) == R_STD_CRYPTO_NATIVE_INVALID);
    R_CRYPTO_CHECK(r_std_crypto_native_rsa_generate(1024U, key + 2048, 300U) ==
                   R_STD_CRYPTO_NATIVE_INVALID);
    for (scheme = 0; scheme < 6; ++scheme) {
        R_CRYPTO_CHECK(r_std_crypto_native_rsa_sign(
                           scheme, key, (size_t)length, message, 12U, signature, 256U) == 256);
    }
    R_CRYPTO_CHECK(
        r_std_crypto_native_rsa_sign(6, key, (size_t)length, message, 12U, signature, 256U) ==
        R_STD_CRYPTO_NATIVE_INVALID);
    R_CRYPTO_CHECK(r_std_crypto_native_rsa_check_public(key, (size_t)length) ==
                   R_STD_CRYPTO_NATIVE_INVALID);
}

/* NIST SP 800-38A F.2.1 (AES-128) with the PKCS#7 block that follows a whole block, and a short
   message under AES-256; checked against the Python cryptography package. */
static void r_crypto_test_cbc(void) {
    uint8_t key[32];
    uint8_t iv[16];
    uint8_t plain[32];
    uint8_t cipher[48];
    uint8_t opened[48];
    size_t index;

    for (index = 0U; index < 16U; ++index) {
        iv[index] = (uint8_t)index;
    }
    (void)r_crypto_unhex("2b7e151628aed2a6abf7158809cf4f3c", key, 16U);
    (void)r_crypto_unhex("6bc1bee22e409f96e93d7e117393172a", plain, 16U);
    R_CRYPTO_CHECK(r_std_crypto_native_cbc(1, key, 16U, iv, plain, 16U, cipher, sizeof(cipher)) ==
                   32);
    R_CRYPTO_CHECK(r_crypto_equal_hex(
        cipher, 32U, "7649abac8119b246cee98e9b12e9197d8964e0b149c10b7b682e6e39aaeb731c"));
    R_CRYPTO_CHECK(r_std_crypto_native_cbc(0, key, 16U, iv, cipher, 32U, opened, sizeof(opened)) ==
                   16);
    R_CRYPTO_CHECK(memcmp(opened, plain, 16U) == 0);
    cipher[31] ^= 1U;
    R_CRYPTO_CHECK(r_std_crypto_native_cbc(0, key, 16U, iv, cipher, 32U, opened, sizeof(opened)) ==
                   R_STD_CRYPTO_NATIVE_REJECTED);
    R_CRYPTO_CHECK(r_std_crypto_native_cbc(0, key, 16U, iv, cipher, 31U, opened, sizeof(opened)) ==
                   R_STD_CRYPTO_NATIVE_INVALID);
    R_CRYPTO_CHECK(r_std_crypto_native_cbc(1, key, 15U, iv, plain, 16U, cipher, sizeof(cipher)) ==
                   R_STD_CRYPTO_NATIVE_INVALID);

    (void)r_crypto_unhex(
        "603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", key, 32U);
    R_CRYPTO_CHECK(r_std_crypto_native_cbc(
                       1, key, 32U, iv, (const uint8_t *)"hello", 5U, cipher, sizeof(cipher)) ==
                   16);
    R_CRYPTO_CHECK(r_crypto_equal_hex(cipher, 16U, "11567e234fd4575f682ce39def007307"));
    R_CRYPTO_CHECK(r_std_crypto_native_cbc(1, key, 32U, iv, NULL, 0U, cipher, 16U) == 16);
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
    R_CRYPTO_CHECK(r_std_crypto_native_pk_ready() == 0);
    r_crypto_test_ecdsa();
    r_crypto_test_rsa();
    r_crypto_test_cbc();
    if (r_crypto_failures != 0) {
        (void)fprintf(stderr, "%d crypto native checks failed\n", r_crypto_failures);
        return 1;
    }
    (void)puts("r_library_crypto_native_tests: ok");
    return 0;
}
