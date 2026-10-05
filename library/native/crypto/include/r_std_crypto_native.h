#ifndef R_STD_CRYPTO_NATIVE_H
#define R_STD_CRYPTO_NATIVE_H

/* The native provider of std.crypto (Library R-SLIB-CRYPTO-0001): libsodium primitives and the
   public-key and block-cipher primitives of Mbed TLS 3.6 (PSA Crypto) behind functions that read
   and write caller buffers only. Every function returns 0, or a length, on success; a negative
   status reports a refused operation, as each function states. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The size of the BLAKE2b state that the caller keeps between calls. */
#define R_STD_CRYPTO_NATIVE_BLAKE2B_STATE_BYTES 384U

/* The size of the text of a password hash, its terminating zero included. */
#define R_STD_CRYPTO_NATIVE_PASSWORD_HASH_BYTES 128U

/* 0 when libsodium is ready, -1 when it cannot start. */
int32_t r_std_crypto_native_ready(void);

/* algorithm 0 is ChaCha20-Poly1305 (RFC 8439, 12-byte nonce), 1 is XChaCha20-Poly1305
   (24-byte nonce); the key has 32 bytes. Seal writes message_length + 16 bytes to target;
   open writes ciphertext_length - 16 bytes and returns -1 when authentication fails. */
int32_t r_std_crypto_native_aead_seal(int32_t algorithm,
                                      const uint8_t *key,
                                      const uint8_t *nonce,
                                      const uint8_t *aad,
                                      size_t aad_length,
                                      const uint8_t *message,
                                      size_t message_length,
                                      uint8_t *target);
int32_t r_std_crypto_native_aead_open(int32_t algorithm,
                                      const uint8_t *key,
                                      const uint8_t *nonce,
                                      const uint8_t *aad,
                                      size_t aad_length,
                                      const uint8_t *ciphertext,
                                      size_t ciphertext_length,
                                      uint8_t *target);

/* Ed25519: a key pair from a 32-byte seed (public 32 bytes, secret 64 bytes), a detached
   64-byte signature, and its verification (-1 when it does not verify). */
int32_t r_std_crypto_native_sign_seed_keypair(const uint8_t *seed,
                                              uint8_t *public_key,
                                              uint8_t *secret_key);
int32_t r_std_crypto_native_sign(uint8_t *signature,
                                 const uint8_t *message,
                                 size_t message_length,
                                 const uint8_t *secret_key);
int32_t r_std_crypto_native_verify(const uint8_t *signature,
                                   const uint8_t *message,
                                   size_t message_length,
                                   const uint8_t *public_key);

/* X25519: the public key of a 32-byte secret, and the shared secret with a peer public key
   (-1 when the result is all zero, for a peer key of small order). */
int32_t r_std_crypto_native_exchange_public(uint8_t *public_key, const uint8_t *secret_key);
int32_t r_std_crypto_native_exchange_shared(uint8_t *shared,
                                            const uint8_t *secret_key,
                                            const uint8_t *peer_key);

/* HKDF of RFC 5869 with SHA-256 (hash 256) or SHA-512 (hash 512); -1 when the length exceeds
   255 hash lengths. */
int32_t r_std_crypto_native_hkdf(int32_t hash,
                                 const uint8_t *salt,
                                 size_t salt_length,
                                 const uint8_t *key_material,
                                 size_t key_material_length,
                                 const uint8_t *info,
                                 size_t info_length,
                                 uint8_t *target,
                                 size_t target_length);

/* Argon2id version 1.3 with a 16-byte salt; -1 for limits outside the ranges of libsodium,
   -2 when the memory cannot be allocated. */
int32_t r_std_crypto_native_argon2id(uint8_t *target,
                                     size_t target_length,
                                     const uint8_t *password,
                                     size_t password_length,
                                     const uint8_t *salt,
                                     uint64_t operations,
                                     size_t memory);
/* The encoded Argon2id hash of a password with a random salt, zero-terminated in target
   (R_STD_CRYPTO_NATIVE_PASSWORD_HASH_BYTES bytes), and its check (-1 when it does not match). */
int32_t r_std_crypto_native_password_hash(uint8_t *target,
                                          const uint8_t *password,
                                          size_t password_length,
                                          uint64_t operations,
                                          size_t memory);
int32_t r_std_crypto_native_password_verify(const uint8_t *hash,
                                            size_t hash_length,
                                            const uint8_t *password,
                                            size_t password_length);

/* BLAKE2b with a digest of 16 to 64 bytes and a key of 0 to 64 bytes; the state of the
   streaming form lives in caller memory of R_STD_CRYPTO_NATIVE_BLAKE2B_STATE_BYTES bytes. */
int32_t r_std_crypto_native_blake2b(uint8_t *target,
                                    size_t target_length,
                                    const uint8_t *data,
                                    size_t data_length,
                                    const uint8_t *key,
                                    size_t key_length);
int32_t r_std_crypto_native_blake2b_start(uint8_t *state,
                                          const uint8_t *key,
                                          size_t key_length,
                                          size_t digest_length);
int32_t r_std_crypto_native_blake2b_update(uint8_t *state, const uint8_t *data, size_t data_length);
int32_t r_std_crypto_native_blake2b_finish(uint8_t *state, uint8_t *target, size_t target_length);

/* Fills target with random bytes from the system source of libsodium. */
int32_t r_std_crypto_native_random(uint8_t *target, size_t length);

/* ---- Mbed TLS (PSA Crypto) ---- */

/* The statuses of the functions below: the input does not hold a valid key, parameter or
   ciphertext length; a signature does not verify or a padding does not check; memory ran out;
   the PSA subsystem cannot start or failed otherwise. */
#define R_STD_CRYPTO_NATIVE_INVALID (-1)
#define R_STD_CRYPTO_NATIVE_REJECTED (-2)
#define R_STD_CRYPTO_NATIVE_EXHAUSTED (-3)
#define R_STD_CRYPTO_NATIVE_FAILED (-4)

/* 0 when PSA Crypto is ready, R_STD_CRYPTO_NATIVE_FAILED when it cannot start. */
int32_t r_std_crypto_native_pk_ready(void);

/* ECDSA. curve 0 is P-256 (secp256r1) with SHA-256, 1 is P-384 (secp384r1) with SHA-384. A
   private scalar has 32 or 48 big-endian bytes, a public point is the uncompressed SEC1 form
   of 65 or 97 bytes and a signature is r and s of 32 or 48 bytes each. Signatures are the
   deterministic ones of RFC 6979. Verification returns 0 for a valid signature,
   R_STD_CRYPTO_NATIVE_REJECTED for another one and R_STD_CRYPTO_NATIVE_INVALID for a point
   that is not on the curve. */
int32_t r_std_crypto_native_ecdsa_generate(int32_t curve, uint8_t *scalar, uint8_t *point);
int32_t r_std_crypto_native_ecdsa_public(int32_t curve, const uint8_t *scalar, uint8_t *point);
int32_t r_std_crypto_native_ecdsa_check(int32_t curve, const uint8_t *point, size_t point_length);
int32_t r_std_crypto_native_ecdsa_sign(int32_t curve,
                                       const uint8_t *scalar,
                                       const uint8_t *message,
                                       size_t message_length,
                                       uint8_t *signature);
int32_t r_std_crypto_native_ecdsa_verify(int32_t curve,
                                         const uint8_t *point,
                                         size_t point_length,
                                         const uint8_t *message,
                                         size_t message_length,
                                         const uint8_t *signature,
                                         size_t signature_length);

/* RSA. A private key is the DER of the PKCS#1 RSAPrivateKey, a public key the DER of the
   PKCS#1 RSAPublicKey. scheme 0 to 2 is PKCS#1 v1.5 with SHA-256, SHA-384 and SHA-512, 3 to 5
   is PSS with those hashes, MGF1 of the same hash and a salt of the hash length (RFC 8017).
   generate writes the private key and returns its length; the check functions return the
   size of the modulus in bits; sign writes a signature of the modulus size and returns its
   length; verify returns as for ECDSA. Keys of more than 4096 bits are invalid. */
int32_t r_std_crypto_native_rsa_generate(uint32_t bits, uint8_t *target, size_t capacity);
int32_t r_std_crypto_native_rsa_check_private(const uint8_t *key, size_t key_length);
int32_t r_std_crypto_native_rsa_check_public(const uint8_t *key, size_t key_length);
int32_t r_std_crypto_native_rsa_sign(int32_t scheme,
                                     const uint8_t *key,
                                     size_t key_length,
                                     const uint8_t *message,
                                     size_t message_length,
                                     uint8_t *signature,
                                     size_t capacity);
int32_t r_std_crypto_native_rsa_verify(int32_t scheme,
                                       const uint8_t *key,
                                       size_t key_length,
                                       const uint8_t *message,
                                       size_t message_length,
                                       const uint8_t *signature,
                                       size_t signature_length);

/* AES in CBC mode with PKCS#7 padding (RFC 5652 section 6.3) and a 16-byte IV; the key has 16,
   24 or 32 bytes. encrypt 1 encrypts, 0 decrypts. The target holds input_length + 16 bytes;
   the function returns the length it wrote, R_STD_CRYPTO_NATIVE_INVALID for a ciphertext
   whose length is not a positive multiple of 16 and R_STD_CRYPTO_NATIVE_REJECTED for a
   padding that does not check. */
int64_t r_std_crypto_native_cbc(int32_t encrypt,
                                const uint8_t *key,
                                size_t key_length,
                                const uint8_t *iv,
                                const uint8_t *input,
                                size_t input_length,
                                uint8_t *target,
                                size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
