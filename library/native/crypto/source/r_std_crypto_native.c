#include "r_std_crypto_native.h"

#include <sodium.h>

#include <stdalign.h>
#include <string.h>

_Static_assert(sizeof(crypto_generichash_blake2b_state) <= R_STD_CRYPTO_NATIVE_BLAKE2B_STATE_BYTES,
               "the BLAKE2b state fits the caller buffer");
_Static_assert(crypto_pwhash_STRBYTES <= R_STD_CRYPTO_NATIVE_PASSWORD_HASH_BYTES,
               "an encoded password hash fits the caller buffer");

/* sodium_init is safe to call more than once and from several threads. */
int32_t r_std_crypto_native_ready(void) {
    return sodium_init() < 0 ? -1 : 0;
}

int32_t r_std_crypto_native_aead_seal(int32_t algorithm,
                                      const uint8_t *key,
                                      const uint8_t *nonce,
                                      const uint8_t *aad,
                                      size_t aad_length,
                                      const uint8_t *message,
                                      size_t message_length,
                                      uint8_t *target) {
    unsigned long long written = 0U;
    int status;

    if (r_std_crypto_native_ready() != 0) {
        return -1;
    }
    if (algorithm == 0) {
        status = crypto_aead_chacha20poly1305_ietf_encrypt(target,
                                                           &written,
                                                           message,
                                                           (unsigned long long)message_length,
                                                           aad,
                                                           (unsigned long long)aad_length,
                                                           NULL,
                                                           nonce,
                                                           key);
    } else {
        status = crypto_aead_xchacha20poly1305_ietf_encrypt(target,
                                                            &written,
                                                            message,
                                                            (unsigned long long)message_length,
                                                            aad,
                                                            (unsigned long long)aad_length,
                                                            NULL,
                                                            nonce,
                                                            key);
    }
    return (status == 0) && (written == (unsigned long long)message_length + 16U) ? 0 : -1;
}

int32_t r_std_crypto_native_aead_open(int32_t algorithm,
                                      const uint8_t *key,
                                      const uint8_t *nonce,
                                      const uint8_t *aad,
                                      size_t aad_length,
                                      const uint8_t *ciphertext,
                                      size_t ciphertext_length,
                                      uint8_t *target) {
    unsigned long long written = 0U;
    int status;

    if ((r_std_crypto_native_ready() != 0) || (ciphertext_length < 16U)) {
        return -1;
    }
    if (algorithm == 0) {
        status = crypto_aead_chacha20poly1305_ietf_decrypt(target,
                                                           &written,
                                                           NULL,
                                                           ciphertext,
                                                           (unsigned long long)ciphertext_length,
                                                           aad,
                                                           (unsigned long long)aad_length,
                                                           nonce,
                                                           key);
    } else {
        status = crypto_aead_xchacha20poly1305_ietf_decrypt(target,
                                                            &written,
                                                            NULL,
                                                            ciphertext,
                                                            (unsigned long long)ciphertext_length,
                                                            aad,
                                                            (unsigned long long)aad_length,
                                                            nonce,
                                                            key);
    }
    return status == 0 ? 0 : -1;
}

int32_t r_std_crypto_native_sign_seed_keypair(const uint8_t *seed,
                                              uint8_t *public_key,
                                              uint8_t *secret_key) {
    if (r_std_crypto_native_ready() != 0) {
        return -1;
    }
    return crypto_sign_ed25519_seed_keypair(public_key, secret_key, seed) == 0 ? 0 : -1;
}

int32_t r_std_crypto_native_sign(uint8_t *signature,
                                 const uint8_t *message,
                                 size_t message_length,
                                 const uint8_t *secret_key) {
    if (r_std_crypto_native_ready() != 0) {
        return -1;
    }
    return crypto_sign_ed25519_detached(
               signature, NULL, message, (unsigned long long)message_length, secret_key) == 0
               ? 0
               : -1;
}

int32_t r_std_crypto_native_verify(const uint8_t *signature,
                                   const uint8_t *message,
                                   size_t message_length,
                                   const uint8_t *public_key) {
    if (r_std_crypto_native_ready() != 0) {
        return -1;
    }
    return crypto_sign_ed25519_verify_detached(
               signature, message, (unsigned long long)message_length, public_key) == 0
               ? 0
               : -1;
}

int32_t r_std_crypto_native_exchange_public(uint8_t *public_key, const uint8_t *secret_key) {
    if (r_std_crypto_native_ready() != 0) {
        return -1;
    }
    return crypto_scalarmult_curve25519_base(public_key, secret_key) == 0 ? 0 : -1;
}

int32_t r_std_crypto_native_exchange_shared(uint8_t *shared,
                                            const uint8_t *secret_key,
                                            const uint8_t *peer_key) {
    if (r_std_crypto_native_ready() != 0) {
        return -1;
    }
    /* libsodium refuses a result of all zero bytes, the output for a small-order peer key. */
    return crypto_scalarmult_curve25519(shared, secret_key, peer_key) == 0 ? 0 : -1;
}

int32_t r_std_crypto_native_hkdf(int32_t hash,
                                 const uint8_t *salt,
                                 size_t salt_length,
                                 const uint8_t *key_material,
                                 size_t key_material_length,
                                 const uint8_t *info,
                                 size_t info_length,
                                 uint8_t *target,
                                 size_t target_length) {
    int status;

    if (r_std_crypto_native_ready() != 0) {
        return -1;
    }
    if (hash == 256) {
        unsigned char key[crypto_kdf_hkdf_sha256_KEYBYTES];
        if (target_length > crypto_kdf_hkdf_sha256_BYTES_MAX) {
            return -1;
        }
        status = crypto_kdf_hkdf_sha256_extract(
            key, salt, salt_length, key_material, key_material_length);
        if (status == 0) {
            status = crypto_kdf_hkdf_sha256_expand(
                target, target_length, (const char *)info, info_length, key);
        }
        sodium_memzero(key, sizeof(key));
        return status == 0 ? 0 : -1;
    }
    {
        unsigned char key[crypto_kdf_hkdf_sha512_KEYBYTES];
        if (target_length > crypto_kdf_hkdf_sha512_BYTES_MAX) {
            return -1;
        }
        status = crypto_kdf_hkdf_sha512_extract(
            key, salt, salt_length, key_material, key_material_length);
        if (status == 0) {
            status = crypto_kdf_hkdf_sha512_expand(
                target, target_length, (const char *)info, info_length, key);
        }
        sodium_memzero(key, sizeof(key));
        return status == 0 ? 0 : -1;
    }
}

static int32_t
r_std_crypto_native_password_limits(size_t length, uint64_t operations, size_t memory) {
    if ((length < crypto_pwhash_PASSWD_MIN) || (length > crypto_pwhash_PASSWD_MAX) ||
        (operations < crypto_pwhash_OPSLIMIT_MIN) || (operations > crypto_pwhash_OPSLIMIT_MAX) ||
        (memory < crypto_pwhash_MEMLIMIT_MIN) || (memory > crypto_pwhash_MEMLIMIT_MAX)) {
        return -1;
    }
    return 0;
}

int32_t r_std_crypto_native_argon2id(uint8_t *target,
                                     size_t target_length,
                                     const uint8_t *password,
                                     size_t password_length,
                                     const uint8_t *salt,
                                     uint64_t operations,
                                     size_t memory) {
    if ((r_std_crypto_native_ready() != 0) || (target_length < crypto_pwhash_BYTES_MIN) ||
        (target_length > crypto_pwhash_BYTES_MAX) ||
        (r_std_crypto_native_password_limits(password_length, operations, memory) != 0)) {
        return -1;
    }
    /* A failure inside libsodium with valid limits is a failed allocation. */
    return crypto_pwhash(target,
                         (unsigned long long)target_length,
                         (const char *)password,
                         (unsigned long long)password_length,
                         salt,
                         (unsigned long long)operations,
                         memory,
                         crypto_pwhash_ALG_ARGON2ID13) == 0
               ? 0
               : -2;
}

int32_t r_std_crypto_native_password_hash(uint8_t *target,
                                          const uint8_t *password,
                                          size_t password_length,
                                          uint64_t operations,
                                          size_t memory) {
    char text[crypto_pwhash_STRBYTES];

    if ((r_std_crypto_native_ready() != 0) ||
        (r_std_crypto_native_password_limits(password_length, operations, memory) != 0)) {
        return -1;
    }
    if (crypto_pwhash_str_alg(text,
                              (const char *)password,
                              (unsigned long long)password_length,
                              (unsigned long long)operations,
                              memory,
                              crypto_pwhash_ALG_ARGON2ID13) != 0) {
        return -2;
    }
    memset(target, 0, R_STD_CRYPTO_NATIVE_PASSWORD_HASH_BYTES);
    memcpy(target, text, strnlen(text, sizeof(text)));
    return 0;
}

int32_t r_std_crypto_native_password_verify(const uint8_t *hash,
                                            size_t hash_length,
                                            const uint8_t *password,
                                            size_t password_length) {
    char text[crypto_pwhash_STRBYTES];

    if ((r_std_crypto_native_ready() != 0) || (hash_length >= sizeof(text)) ||
        (password_length > crypto_pwhash_PASSWD_MAX)) {
        return -1;
    }
    memset(text, 0, sizeof(text));
    memcpy(text, hash, hash_length);
    return crypto_pwhash_str_verify(
               text, (const char *)password, (unsigned long long)password_length) == 0
               ? 0
               : -1;
}

int32_t r_std_crypto_native_blake2b(uint8_t *target,
                                    size_t target_length,
                                    const uint8_t *data,
                                    size_t data_length,
                                    const uint8_t *key,
                                    size_t key_length) {
    if ((r_std_crypto_native_ready() != 0) ||
        (target_length < crypto_generichash_blake2b_BYTES_MIN) ||
        (target_length > crypto_generichash_blake2b_BYTES_MAX) ||
        (key_length > crypto_generichash_blake2b_KEYBYTES_MAX)) {
        return -1;
    }
    return crypto_generichash_blake2b(
               target, target_length, data, (unsigned long long)data_length, key, key_length) == 0
               ? 0
               : -1;
}

/* The state is kept in caller memory without the alignment libsodium needs, so each call
   works on an aligned copy. */
int32_t r_std_crypto_native_blake2b_start(uint8_t *state,
                                          const uint8_t *key,
                                          size_t key_length,
                                          size_t digest_length) {
    crypto_generichash_blake2b_state aligned;
    int status;

    if ((r_std_crypto_native_ready() != 0) ||
        (digest_length < crypto_generichash_blake2b_BYTES_MIN) ||
        (digest_length > crypto_generichash_blake2b_BYTES_MAX) ||
        (key_length > crypto_generichash_blake2b_KEYBYTES_MAX)) {
        return -1;
    }
    status = crypto_generichash_blake2b_init(&aligned, key, key_length, digest_length);
    memcpy(state, &aligned, sizeof(aligned));
    sodium_memzero(&aligned, sizeof(aligned));
    return status == 0 ? 0 : -1;
}

int32_t
r_std_crypto_native_blake2b_update(uint8_t *state, const uint8_t *data, size_t data_length) {
    crypto_generichash_blake2b_state aligned;
    int status;

    memcpy(&aligned, state, sizeof(aligned));
    status = crypto_generichash_blake2b_update(&aligned, data, (unsigned long long)data_length);
    memcpy(state, &aligned, sizeof(aligned));
    sodium_memzero(&aligned, sizeof(aligned));
    return status == 0 ? 0 : -1;
}

int32_t r_std_crypto_native_blake2b_finish(uint8_t *state, uint8_t *target, size_t target_length) {
    crypto_generichash_blake2b_state aligned;
    int status;

    memcpy(&aligned, state, sizeof(aligned));
    status = crypto_generichash_blake2b_final(&aligned, target, target_length);
    sodium_memzero(&aligned, sizeof(aligned));
    sodium_memzero(state, R_STD_CRYPTO_NATIVE_BLAKE2B_STATE_BYTES);
    return status == 0 ? 0 : -1;
}

int32_t r_std_crypto_native_random(uint8_t *target, size_t length) {
    if (r_std_crypto_native_ready() != 0) {
        return -1;
    }
    randombytes_buf(target, length);
    return 0;
}
