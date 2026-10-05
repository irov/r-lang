#include "r_std_crypto_native.h"

#include <mbedtls/platform_util.h>
#include <psa/crypto.h>
#include <sodium.h>

#include <pthread.h>
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

/* ---- Mbed TLS (PSA Crypto) ---- */

/* PSA Crypto keeps its keys in a store guarded by its own mutexes (MBEDTLS_THREADING_C), so
   every operation imports the key it needs as a volatile key, uses it and destroys it; no key
   outlives a call. psa_crypto_init may run from several providers and threads. */
static pthread_once_t r_std_crypto_native_psa_once = PTHREAD_ONCE_INIT;
static psa_status_t r_std_crypto_native_psa_status = PSA_ERROR_GENERIC_ERROR;

static void r_std_crypto_native_psa_start(void) {
    r_std_crypto_native_psa_status = psa_crypto_init();
}

int32_t r_std_crypto_native_pk_ready(void) {
    if ((pthread_once(&r_std_crypto_native_psa_once, r_std_crypto_native_psa_start) != 0) ||
        (r_std_crypto_native_psa_status != PSA_SUCCESS)) {
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
    return 0;
}

static int32_t r_std_crypto_native_status(psa_status_t status) {
    switch (status) {
    case PSA_SUCCESS:
        return 0;
    case PSA_ERROR_INVALID_ARGUMENT:
    case PSA_ERROR_NOT_SUPPORTED:
    case PSA_ERROR_BUFFER_TOO_SMALL:
        return R_STD_CRYPTO_NATIVE_INVALID;
    case PSA_ERROR_INVALID_SIGNATURE:
    case PSA_ERROR_INVALID_PADDING:
        return R_STD_CRYPTO_NATIVE_REJECTED;
    case PSA_ERROR_INSUFFICIENT_MEMORY:
        return R_STD_CRYPTO_NATIVE_EXHAUSTED;
    default:
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
}

/* The scalar size of a curve, 0 for an unknown curve. */
static size_t r_std_crypto_native_curve_bytes(int32_t curve) {
    if (curve == 0) {
        return 32U;
    }
    if (curve == 1) {
        return 48U;
    }
    return 0U;
}

static psa_algorithm_t r_std_crypto_native_curve_algorithm(int32_t curve) {
    return curve == 0 ? PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_SHA_256)
                      : PSA_ALG_DETERMINISTIC_ECDSA(PSA_ALG_SHA_384);
}

/* Imports an ECDSA key: the private scalar (pair) or the uncompressed public point. */
static psa_status_t r_std_crypto_native_ecdsa_import(int32_t curve,
                                                     int pair,
                                                     psa_key_usage_t usage,
                                                     const uint8_t *data,
                                                     size_t length,
                                                     mbedtls_svc_key_id_t *key) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_status_t status;

    psa_set_key_type(&attributes,
                     pair ? PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1)
                          : PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, r_std_crypto_native_curve_bytes(curve) * 8U);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, r_std_crypto_native_curve_algorithm(curve));
    status = psa_import_key(&attributes, data, length, key);
    psa_reset_key_attributes(&attributes);
    return status;
}

int32_t r_std_crypto_native_ecdsa_generate(int32_t curve, uint8_t *scalar, uint8_t *point) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    size_t size = r_std_crypto_native_curve_bytes(curve);
    size_t written = 0U;
    psa_status_t status;

    if ((r_std_crypto_native_pk_ready() != 0) || (size == 0U)) {
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
    psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
    psa_set_key_bits(&attributes, size * 8U);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT);
    psa_set_key_algorithm(&attributes, r_std_crypto_native_curve_algorithm(curve));
    status = psa_generate_key(&attributes, &key);
    psa_reset_key_attributes(&attributes);
    if (status == PSA_SUCCESS) {
        status = psa_export_key(key, scalar, size, &written);
        if ((status == PSA_SUCCESS) && (written != size)) {
            status = PSA_ERROR_GENERIC_ERROR;
        }
    }
    if (status == PSA_SUCCESS) {
        status = psa_export_public_key(key, point, size * 2U + 1U, &written);
        if ((status == PSA_SUCCESS) && (written != size * 2U + 1U)) {
            status = PSA_ERROR_GENERIC_ERROR;
        }
    }
    (void)psa_destroy_key(key);
    if (status != PSA_SUCCESS) {
        mbedtls_platform_zeroize(scalar, size);
    }
    return r_std_crypto_native_status(status);
}

int32_t r_std_crypto_native_ecdsa_public(int32_t curve, const uint8_t *scalar, uint8_t *point) {
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    size_t size = r_std_crypto_native_curve_bytes(curve);
    size_t written = 0U;
    psa_status_t status;

    if ((r_std_crypto_native_pk_ready() != 0) || (size == 0U)) {
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
    status = r_std_crypto_native_ecdsa_import(curve, 1, 0U, scalar, size, &key);
    if (status == PSA_SUCCESS) {
        status = psa_export_public_key(key, point, size * 2U + 1U, &written);
        if ((status == PSA_SUCCESS) && (written != size * 2U + 1U)) {
            status = PSA_ERROR_GENERIC_ERROR;
        }
        (void)psa_destroy_key(key);
    }
    return r_std_crypto_native_status(status);
}

int32_t r_std_crypto_native_ecdsa_check(int32_t curve, const uint8_t *point, size_t point_length) {
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    size_t size = r_std_crypto_native_curve_bytes(curve);
    psa_status_t status;

    if ((r_std_crypto_native_pk_ready() != 0) || (size == 0U)) {
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
    if (point_length != size * 2U + 1U) {
        return R_STD_CRYPTO_NATIVE_INVALID;
    }
    status = r_std_crypto_native_ecdsa_import(curve, 0, 0U, point, point_length, &key);
    if (status == PSA_SUCCESS) {
        (void)psa_destroy_key(key);
    }
    return r_std_crypto_native_status(status);
}

int32_t r_std_crypto_native_ecdsa_sign(int32_t curve,
                                       const uint8_t *scalar,
                                       const uint8_t *message,
                                       size_t message_length,
                                       uint8_t *signature) {
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    size_t size = r_std_crypto_native_curve_bytes(curve);
    size_t written = 0U;
    psa_status_t status;

    if ((r_std_crypto_native_pk_ready() != 0) || (size == 0U)) {
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
    status =
        r_std_crypto_native_ecdsa_import(curve, 1, PSA_KEY_USAGE_SIGN_MESSAGE, scalar, size, &key);
    if (status == PSA_SUCCESS) {
        status = psa_sign_message(key,
                                  r_std_crypto_native_curve_algorithm(curve),
                                  message,
                                  message_length,
                                  signature,
                                  size * 2U,
                                  &written);
        if ((status == PSA_SUCCESS) && (written != size * 2U)) {
            status = PSA_ERROR_GENERIC_ERROR;
        }
        (void)psa_destroy_key(key);
    }
    return r_std_crypto_native_status(status);
}

int32_t r_std_crypto_native_ecdsa_verify(int32_t curve,
                                         const uint8_t *point,
                                         size_t point_length,
                                         const uint8_t *message,
                                         size_t message_length,
                                         const uint8_t *signature,
                                         size_t signature_length) {
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    size_t size = r_std_crypto_native_curve_bytes(curve);
    psa_status_t status;

    if ((r_std_crypto_native_pk_ready() != 0) || (size == 0U)) {
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
    if (point_length != size * 2U + 1U) {
        return R_STD_CRYPTO_NATIVE_INVALID;
    }
    if (signature_length != size * 2U) {
        return R_STD_CRYPTO_NATIVE_REJECTED;
    }
    status = r_std_crypto_native_ecdsa_import(
        curve, 0, PSA_KEY_USAGE_VERIFY_MESSAGE, point, point_length, &key);
    if (status != PSA_SUCCESS) {
        return R_STD_CRYPTO_NATIVE_INVALID;
    }
    /* A deterministic ECDSA key verifies every ECDSA signature of its hash. */
    status = psa_verify_message(key,
                                r_std_crypto_native_curve_algorithm(curve),
                                message,
                                message_length,
                                signature,
                                signature_length);
    (void)psa_destroy_key(key);
    return r_std_crypto_native_status(status);
}

static psa_algorithm_t r_std_crypto_native_rsa_algorithm(int32_t scheme) {
    switch (scheme) {
    case 0:
        return PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_256);
    case 1:
        return PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_384);
    case 2:
        return PSA_ALG_RSA_PKCS1V15_SIGN(PSA_ALG_SHA_512);
    case 3:
        return PSA_ALG_RSA_PSS(PSA_ALG_SHA_256);
    case 4:
        return PSA_ALG_RSA_PSS(PSA_ALG_SHA_384);
    case 5:
        return PSA_ALG_RSA_PSS(PSA_ALG_SHA_512);
    default:
        return PSA_ALG_NONE;
    }
}

static psa_status_t r_std_crypto_native_rsa_import(int pair,
                                                   psa_key_usage_t usage,
                                                   psa_algorithm_t algorithm,
                                                   const uint8_t *data,
                                                   size_t length,
                                                   mbedtls_svc_key_id_t *key) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_status_t status;

    psa_set_key_type(&attributes, pair ? PSA_KEY_TYPE_RSA_KEY_PAIR : PSA_KEY_TYPE_RSA_PUBLIC_KEY);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, algorithm);
    status = psa_import_key(&attributes, data, length, key);
    psa_reset_key_attributes(&attributes);
    return status;
}

/* The size in bits of an imported key, 0 when it cannot be read. */
static size_t r_std_crypto_native_key_bits(mbedtls_svc_key_id_t key) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    size_t bits = 0U;

    if (psa_get_key_attributes(key, &attributes) == PSA_SUCCESS) {
        bits = psa_get_key_bits(&attributes);
    }
    psa_reset_key_attributes(&attributes);
    return bits;
}

int32_t r_std_crypto_native_rsa_generate(uint32_t bits, uint8_t *target, size_t capacity) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    size_t written = 0U;
    psa_status_t status;

    if (r_std_crypto_native_pk_ready() != 0) {
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
    if ((bits < 2048U) || (bits > PSA_VENDOR_RSA_MAX_KEY_BITS) || ((bits % 8U) != 0U)) {
        return R_STD_CRYPTO_NATIVE_INVALID;
    }
    psa_set_key_type(&attributes, PSA_KEY_TYPE_RSA_KEY_PAIR);
    psa_set_key_bits(&attributes, bits);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_EXPORT);
    status = psa_generate_key(&attributes, &key);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        return r_std_crypto_native_status(status);
    }
    status = psa_export_key(key, target, capacity, &written);
    (void)psa_destroy_key(key);
    if ((status != PSA_SUCCESS) || (written > (size_t)INT32_MAX)) {
        mbedtls_platform_zeroize(target, capacity);
        return r_std_crypto_native_status(status == PSA_SUCCESS ? PSA_ERROR_GENERIC_ERROR : status);
    }
    return (int32_t)written;
}

static int32_t r_std_crypto_native_rsa_check(int pair, const uint8_t *key_data, size_t key_length) {
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    size_t bits;
    psa_status_t status;

    if (r_std_crypto_native_pk_ready() != 0) {
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
    status = r_std_crypto_native_rsa_import(pair, 0U, PSA_ALG_NONE, key_data, key_length, &key);
    if (status != PSA_SUCCESS) {
        return status == PSA_ERROR_INSUFFICIENT_MEMORY ? R_STD_CRYPTO_NATIVE_EXHAUSTED
                                                       : R_STD_CRYPTO_NATIVE_INVALID;
    }
    bits = r_std_crypto_native_key_bits(key);
    (void)psa_destroy_key(key);
    if ((bits == 0U) || (bits > PSA_VENDOR_RSA_MAX_KEY_BITS)) {
        return R_STD_CRYPTO_NATIVE_INVALID;
    }
    return (int32_t)bits;
}

int32_t r_std_crypto_native_rsa_check_private(const uint8_t *key, size_t key_length) {
    return r_std_crypto_native_rsa_check(1, key, key_length);
}

int32_t r_std_crypto_native_rsa_check_public(const uint8_t *key, size_t key_length) {
    return r_std_crypto_native_rsa_check(0, key, key_length);
}

int32_t r_std_crypto_native_rsa_sign(int32_t scheme,
                                     const uint8_t *key_data,
                                     size_t key_length,
                                     const uint8_t *message,
                                     size_t message_length,
                                     uint8_t *signature,
                                     size_t capacity) {
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    psa_algorithm_t algorithm = r_std_crypto_native_rsa_algorithm(scheme);
    size_t written = 0U;
    psa_status_t status;

    if (r_std_crypto_native_pk_ready() != 0) {
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
    if (algorithm == PSA_ALG_NONE) {
        return R_STD_CRYPTO_NATIVE_INVALID;
    }
    status = r_std_crypto_native_rsa_import(
        1, PSA_KEY_USAGE_SIGN_MESSAGE, algorithm, key_data, key_length, &key);
    if (status != PSA_SUCCESS) {
        return r_std_crypto_native_status(status);
    }
    status =
        psa_sign_message(key, algorithm, message, message_length, signature, capacity, &written);
    (void)psa_destroy_key(key);
    if ((status != PSA_SUCCESS) || (written > (size_t)INT32_MAX)) {
        return r_std_crypto_native_status(status == PSA_SUCCESS ? PSA_ERROR_GENERIC_ERROR : status);
    }
    return (int32_t)written;
}

int32_t r_std_crypto_native_rsa_verify(int32_t scheme,
                                       const uint8_t *key_data,
                                       size_t key_length,
                                       const uint8_t *message,
                                       size_t message_length,
                                       const uint8_t *signature,
                                       size_t signature_length) {
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    psa_algorithm_t algorithm = r_std_crypto_native_rsa_algorithm(scheme);
    psa_status_t status;

    if (r_std_crypto_native_pk_ready() != 0) {
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
    if (algorithm == PSA_ALG_NONE) {
        return R_STD_CRYPTO_NATIVE_INVALID;
    }
    status = r_std_crypto_native_rsa_import(
        0, PSA_KEY_USAGE_VERIFY_MESSAGE, algorithm, key_data, key_length, &key);
    if (status != PSA_SUCCESS) {
        return status == PSA_ERROR_INSUFFICIENT_MEMORY ? R_STD_CRYPTO_NATIVE_EXHAUSTED
                                                       : R_STD_CRYPTO_NATIVE_INVALID;
    }
    status =
        psa_verify_message(key, algorithm, message, message_length, signature, signature_length);
    (void)psa_destroy_key(key);
    /* A signature of another length than the modulus is a signature that does not verify. */
    if (status == PSA_ERROR_INVALID_ARGUMENT) {
        status = PSA_ERROR_INVALID_SIGNATURE;
    }
    return r_std_crypto_native_status(status);
}

int64_t r_std_crypto_native_cbc(int32_t encrypt,
                                const uint8_t *key_data,
                                size_t key_length,
                                const uint8_t *iv,
                                const uint8_t *input,
                                size_t input_length,
                                uint8_t *target,
                                size_t capacity) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_cipher_operation_t operation = PSA_CIPHER_OPERATION_INIT;
    mbedtls_svc_key_id_t key = MBEDTLS_SVC_KEY_ID_INIT;
    size_t first = 0U;
    size_t last = 0U;
    psa_status_t status;

    if (r_std_crypto_native_pk_ready() != 0) {
        return R_STD_CRYPTO_NATIVE_FAILED;
    }
    if (((key_length != 16U) && (key_length != 24U) && (key_length != 32U)) ||
        (capacity < input_length + 16U) ||
        ((encrypt == 0) && ((input_length == 0U) || ((input_length % 16U) != 0U)))) {
        return R_STD_CRYPTO_NATIVE_INVALID;
    }
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, key_length * 8U);
    psa_set_key_usage_flags(&attributes, encrypt ? PSA_KEY_USAGE_ENCRYPT : PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attributes, PSA_ALG_CBC_PKCS7);
    status = psa_import_key(&attributes, key_data, key_length, &key);
    psa_reset_key_attributes(&attributes);
    if (status != PSA_SUCCESS) {
        return r_std_crypto_native_status(status);
    }
    status = encrypt ? psa_cipher_encrypt_setup(&operation, key, PSA_ALG_CBC_PKCS7)
                     : psa_cipher_decrypt_setup(&operation, key, PSA_ALG_CBC_PKCS7);
    if (status == PSA_SUCCESS) {
        status = psa_cipher_set_iv(&operation, iv, 16U);
    }
    if (status == PSA_SUCCESS) {
        status = psa_cipher_update(&operation, input, input_length, target, capacity, &first);
    }
    if (status == PSA_SUCCESS) {
        status = psa_cipher_finish(&operation, target + first, capacity - first, &last);
    }
    (void)psa_cipher_abort(&operation);
    (void)psa_destroy_key(key);
    if (status != PSA_SUCCESS) {
        mbedtls_platform_zeroize(target, capacity);
        return r_std_crypto_native_status(status);
    }
    return (int64_t)(first + last);
}
