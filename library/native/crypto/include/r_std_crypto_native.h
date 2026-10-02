#ifndef R_STD_CRYPTO_NATIVE_H
#define R_STD_CRYPTO_NATIVE_H

/* The native provider of std.crypto (Library R-SLIB-CRYPTO-0001): libsodium primitives behind
   functions that read and write caller buffers only. Every function returns 0 on success; a
   negative status reports a refused operation, as each function states. */

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

#ifdef __cplusplus
}
#endif

#endif
