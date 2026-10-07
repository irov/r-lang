module std.crypto;
import std.encoding;

/* R-SLIB-CRYPTO-0001: the R part of std.crypto over its native provider std.crypto.native, which
   calls libsodium and the PSA Crypto API of Mbed TLS 3.6. Every provider function reads and
   writes the buffers it is given only. */

@link(name = "std.crypto.native", kind = "static")
@header("r_std_crypto_native.h")
extern "C" {
    @safety("CRYPTO-READY", "The function has no preconditions")
    c_int32 r_std_crypto_native_ready();
    @safety("CRYPTO-AEAD-SEAL", "key, nonce, aad and message address their lengths and target holds message_length + 16 bytes")
    c_int32 r_std_crypto_native_aead_seal(c_int32 algorithm,
                                          raw const c_uint8*? key,
                                          raw const c_uint8*? nonce,
                                          raw const c_uint8*? aad,
                                          c_size aad_length,
                                          raw const c_uint8*? message,
                                          c_size message_length,
                                          raw c_uint8* target);
    @safety("CRYPTO-AEAD-OPEN", "key, nonce, aad and ciphertext address their lengths and target holds ciphertext_length - 16 bytes")
    c_int32 r_std_crypto_native_aead_open(c_int32 algorithm,
                                          raw const c_uint8*? key,
                                          raw const c_uint8*? nonce,
                                          raw const c_uint8*? aad,
                                          c_size aad_length,
                                          raw const c_uint8*? ciphertext,
                                          c_size ciphertext_length,
                                          raw c_uint8*? target);
    @safety("CRYPTO-SIGN-KEYPAIR", "seed addresses 32 bytes, public_key 32 writable bytes and secret_key 64 writable bytes")
    c_int32 r_std_crypto_native_sign_seed_keypair(raw const c_uint8*? seed,
                                                  raw c_uint8* public_key,
                                                  raw c_uint8* secret_key);
    @safety("CRYPTO-SIGN", "signature holds 64 writable bytes, message addresses its length and secret_key 64 bytes")
    c_int32 r_std_crypto_native_sign(raw c_uint8* signature,
                                     raw const c_uint8*? message,
                                     c_size message_length,
                                     raw const c_uint8*? secret_key);
    @safety("CRYPTO-VERIFY", "signature addresses 64 bytes, message its length and public_key 32 bytes")
    c_int32 r_std_crypto_native_verify(raw const c_uint8*? signature,
                                       raw const c_uint8*? message,
                                       c_size message_length,
                                       raw const c_uint8*? public_key);
    @safety("CRYPTO-EXCHANGE-PUBLIC", "public_key holds 32 writable bytes and secret_key addresses 32 bytes")
    c_int32 r_std_crypto_native_exchange_public(raw c_uint8* public_key, raw const c_uint8*? secret_key);
    @safety("CRYPTO-EXCHANGE-SHARED", "shared holds 32 writable bytes and both keys address 32 bytes")
    c_int32 r_std_crypto_native_exchange_shared(raw c_uint8* shared,
                                                raw const c_uint8*? secret_key,
                                                raw const c_uint8*? peer_key);
    @safety("CRYPTO-HKDF", "salt, key_material and info address their lengths and target holds target_length bytes")
    c_int32 r_std_crypto_native_hkdf(c_int32 hash,
                                     raw const c_uint8*? salt,
                                     c_size salt_length,
                                     raw const c_uint8*? key_material,
                                     c_size key_material_length,
                                     raw const c_uint8*? info,
                                     c_size info_length,
                                     raw c_uint8* target,
                                     c_size target_length);
    @safety("CRYPTO-ARGON2ID", "target holds target_length bytes, password addresses its length and salt 16 bytes")
    c_int32 r_std_crypto_native_argon2id(raw c_uint8* target,
                                         c_size target_length,
                                         raw const c_uint8*? password,
                                         c_size password_length,
                                         raw const c_uint8*? salt,
                                         c_uint64 operations,
                                         c_size memory);
    @safety("CRYPTO-PASSWORD-HASH", "target holds 128 writable bytes and password addresses its length")
    c_int32 r_std_crypto_native_password_hash(raw c_uint8* target,
                                              raw const c_uint8*? password,
                                              c_size password_length,
                                              c_uint64 operations,
                                              c_size memory);
    @safety("CRYPTO-PASSWORD-VERIFY", "hash and password address their lengths")
    c_int32 r_std_crypto_native_password_verify(raw const c_uint8*? hash,
                                                c_size hash_length,
                                                raw const c_uint8*? password,
                                                c_size password_length);
    @safety("CRYPTO-BLAKE2B", "target holds target_length bytes and data and key address their lengths")
    c_int32 r_std_crypto_native_blake2b(raw c_uint8* target,
                                        c_size target_length,
                                        raw const c_uint8*? data,
                                        c_size data_length,
                                        raw const c_uint8*? key,
                                        c_size key_length);
    @safety("CRYPTO-BLAKE2B-START", "state holds 384 writable bytes and key addresses its length")
    c_int32 r_std_crypto_native_blake2b_start(raw c_uint8* state,
                                              raw const c_uint8*? key,
                                              c_size key_length,
                                              c_size digest_length);
    @safety("CRYPTO-BLAKE2B-UPDATE", "state holds a started state and data addresses its length")
    c_int32 r_std_crypto_native_blake2b_update(raw c_uint8* state,
                                               raw const c_uint8*? data,
                                               c_size data_length);
    @safety("CRYPTO-BLAKE2B-FINISH", "state holds a started state and target holds target_length bytes")
    c_int32 r_std_crypto_native_blake2b_finish(raw c_uint8* state,
                                               raw c_uint8* target,
                                               c_size target_length);
    @safety("CRYPTO-RANDOM", "target holds length writable bytes")
    c_int32 r_std_crypto_native_random(raw c_uint8* target, c_size length);
    @safety("CRYPTO-PK-READY", "The function has no preconditions")
    c_int32 r_std_crypto_native_pk_ready();
    @safety("CRYPTO-ECDSA-GENERATE",
            "scalar and point hold the scalar and point sizes of the curve as writable bytes")
    c_int32 r_std_crypto_native_ecdsa_generate(c_int32 curve, raw c_uint8* scalar, raw c_uint8* point);
    @safety("CRYPTO-ECDSA-PUBLIC",
            "scalar addresses the scalar size of the curve and point holds its point size as writable bytes")
    c_int32 r_std_crypto_native_ecdsa_public(c_int32 curve, raw const c_uint8*? scalar, raw c_uint8* point);
    @safety("CRYPTO-ECDSA-CHECK", "point addresses point_length bytes")
    c_int32 r_std_crypto_native_ecdsa_check(c_int32 curve, raw const c_uint8*? point, c_size point_length);
    @safety("CRYPTO-ECDSA-SIGN",
            "scalar addresses the scalar size of the curve, message its length and signature twice the scalar size as writable bytes")
    c_int32 r_std_crypto_native_ecdsa_sign(c_int32 curve,
                                           raw const c_uint8*? scalar,
                                           raw const c_uint8*? message,
                                           c_size message_length,
                                           raw c_uint8* signature);
    @safety("CRYPTO-ECDSA-VERIFY", "point, message and signature address their lengths")
    c_int32 r_std_crypto_native_ecdsa_verify(c_int32 curve,
                                             raw const c_uint8*? point,
                                             c_size point_length,
                                             raw const c_uint8*? message,
                                             c_size message_length,
                                             raw const c_uint8*? signature,
                                             c_size signature_length);
    @safety("CRYPTO-RSA-GENERATE", "target holds capacity writable bytes")
    c_int32 r_std_crypto_native_rsa_generate(c_uint32 bits, raw c_uint8* target, c_size capacity);
    @safety("CRYPTO-RSA-CHECK-PRIVATE", "key addresses key_length bytes")
    c_int32 r_std_crypto_native_rsa_check_private(raw const c_uint8*? key, c_size key_length);
    @safety("CRYPTO-RSA-CHECK-PUBLIC", "key addresses key_length bytes")
    c_int32 r_std_crypto_native_rsa_check_public(raw const c_uint8*? key, c_size key_length);
    @safety("CRYPTO-RSA-SIGN",
            "key and message address their lengths and signature holds capacity writable bytes")
    c_int32 r_std_crypto_native_rsa_sign(c_int32 scheme,
                                         raw const c_uint8*? key,
                                         c_size key_length,
                                         raw const c_uint8*? message,
                                         c_size message_length,
                                         raw c_uint8* signature,
                                         c_size capacity);
    @safety("CRYPTO-RSA-VERIFY", "key, message and signature address their lengths")
    c_int32 r_std_crypto_native_rsa_verify(c_int32 scheme,
                                           raw const c_uint8*? key,
                                           c_size key_length,
                                           raw const c_uint8*? message,
                                           c_size message_length,
                                           raw const c_uint8*? signature,
                                           c_size signature_length);
    @safety("CRYPTO-CBC",
            "key and input address their lengths, iv addresses 16 bytes and target holds capacity writable bytes")
    c_int64 r_std_crypto_native_cbc(c_int32 encrypt,
                                    raw const c_uint8*? key,
                                    c_size key_length,
                                    raw const c_uint8*? iv,
                                    raw const c_uint8*? input,
                                    c_size input_length,
                                    raw c_uint8* target,
                                    c_size capacity);
}

/* The address of the first byte of a view as the provider reads it, null for an empty view. */
protected raw const c_uint8*? bytes_of(const u8[] data) {
    if (len(data) == 0usize) { return null; }
    unsafe {
        raw const u8* first = &data[0usize] as raw const u8*;
        raw const void* erased = first as raw const void*;
        return erased as raw const c_uint8*;
    }
}

/* The address of the first byte of a nonempty view as the provider writes it. */
protected raw c_uint8* target_of(u8[] data) {
    unsafe {
        raw u8* first = &data[0usize] as raw u8*;
        raw void* erased = first as raw void*;
        return erased as raw c_uint8*;
    }
}

/* R-SLIB-CRYPTO-0002: why an operation of std.crypto failed. */
@derive(format)
enum error_code {
    invalid_length,
    authentication_failed,
    weak_key,
    limits,
    resource_exhausted,
    unavailable,
    invalid_key,
    invalid_padding,
    unsupported,
};

error crypto_error { error_code code; };

protected crypto_error failure(error_code code) { return crypto_error {.code = code}; }

/* The provider starts libsodium on its first use; a failure to start is reported once more by
   every operation. */
protected void ready() throws crypto_error {
    unsafe {
        if (r_std_crypto_native_ready() != (0i32 as c_int32)) { throw failure(error_code::unavailable); }
    }
}

/* ---- Random bytes ---- */

/* R-SLIB-CRYPTO-0003: `length` bytes from the random source of the system. */
bytes random(usize length) throws std.alloc::alloc_error, crypto_error {
    ready();
    bytes data = std.alloc::bytes(length, 0u8);
    if (length == 0usize) { return move data; }
    unsafe {
        if (r_std_crypto_native_random(target_of(data.as_slice_mut()), length as c_size) != (0i32 as c_int32)) {
            throw failure(error_code::unavailable);
        }
    }
    return move data;
}

/* ---- Authenticated encryption ---- */

/* R-SLIB-CRYPTO-0004: the AEAD constructions of RFC 8439 (ChaCha20-Poly1305, 12-byte nonce) and
   XChaCha20-Poly1305 (24-byte nonce), both with 32-byte keys and 16-byte tags. */
enum aead { chacha20_poly1305, xchacha20_poly1305 };

usize aead::key_length(aead this) {
    this as void;
    return 32usize;
}

usize aead::nonce_length(aead this) {
    if (this == aead::xchacha20_poly1305) { return 24usize; }
    return 12usize;
}

usize aead::tag_length(aead this) {
    this as void;
    return 16usize;
}

protected c_int32 aead_code(aead algorithm) {
    if (algorithm == aead::xchacha20_poly1305) { return 1i32 as c_int32; }
    return 0i32 as c_int32;
}

/* The ciphertext of `plaintext` followed by its tag, authenticating `aad` as well. */
bytes seal(aead algorithm, const u8[] key, const u8[] nonce, const u8[] aad, const u8[] plaintext)
    throws std.alloc::alloc_error, crypto_error {
    ready();
    if (len(key) != algorithm.key_length() || len(nonce) != algorithm.nonce_length()) {
        throw failure(error_code::invalid_length);
    }
    bytes sealed = std.alloc::bytes(len(plaintext) + 16usize, 0u8);
    unsafe {
        c_int32 status = r_std_crypto_native_aead_seal(aead_code(algorithm), bytes_of(key), bytes_of(nonce),
                                                       bytes_of(aad), len(aad) as c_size,
                                                       bytes_of(plaintext), len(plaintext) as c_size,
                                                       target_of(sealed.as_slice_mut()));
        if (status != (0i32 as c_int32)) { throw failure(error_code::unavailable); }
    }
    return move sealed;
}

/* The plaintext of a sealed ciphertext; a ciphertext, tag, nonce, key or `aad` that does not
   authenticate is authentication_failed, and no plaintext is returned. */
bytes open(aead algorithm, const u8[] key, const u8[] nonce, const u8[] aad, const u8[] ciphertext)
    throws std.alloc::alloc_error, crypto_error {
    ready();
    if (len(key) != algorithm.key_length() || len(nonce) != algorithm.nonce_length()) {
        throw failure(error_code::invalid_length);
    }
    if (len(ciphertext) < 16usize) { throw failure(error_code::authentication_failed); }
    bytes opened = std.alloc::bytes(len(ciphertext) - 16usize, 0u8);
    unsafe {
        raw c_uint8*? target = null;
        if (len(opened) > 0usize) { target = target_of(opened.as_slice_mut()); }
        c_int32 status = r_std_crypto_native_aead_open(aead_code(algorithm), bytes_of(key), bytes_of(nonce),
                                                       bytes_of(aad), len(aad) as c_size,
                                                       bytes_of(ciphertext), len(ciphertext) as c_size, target);
        if (status != (0i32 as c_int32)) {
            std.secret::zeroize(opened.as_slice_mut());
            throw failure(error_code::authentication_failed);
        }
    }
    return move opened;
}

/* ---- Ed25519 signatures ---- */

/* R-SLIB-CRYPTO-0005: an Ed25519 key pair of RFC 8032. The 64-byte secret part stays in a
   secret buffer; the public key is the 32-byte encoding of the RFC. */
struct signing_key {
    protected std.secret::buffer secret;
    u8[32] public_key;
};

/* The key pair of a 32-byte seed, deterministic as RFC 8032 section 5.1.5 specifies. */
signing_key signing_key::from_seed(const u8[] seed) throws std.alloc::alloc_error, crypto_error {
    ready();
    if (len(seed) != 32usize) { throw failure(error_code::invalid_length); }
    std.secret::buffer secret = std.secret::with_length(64usize);
    u8[32] public_key = {};
    unsafe {
        c_int32 status = r_std_crypto_native_sign_seed_keypair(bytes_of(seed), target_of(public_key[..]),
                                                               target_of(std.secret::as_slice_mut(&secret)));
        if (status != (0i32 as c_int32)) { throw failure(error_code::unavailable); }
    }
    return signing_key {.secret = move secret, .public_key = public_key};
}

/* A key pair of a seed from the random source. */
signing_key signing_key::generate() throws std.alloc::alloc_error, crypto_error {
    bytes seed = random(32usize);
    std.secret::buffer kept = std.secret::from_bytes(move seed);
    return signing_key::from_seed(std.secret::as_slice(&kept));
}

/* The 64-byte detached signature of `message`. */
u8[64] signing_key::sign(const signing_key* this, const u8[] message) throws crypto_error {
    ready();
    u8[64] signature = {};
    unsafe {
        c_int32 status = r_std_crypto_native_sign(target_of(signature[..]), bytes_of(message),
                                                  len(message) as c_size,
                                                  bytes_of(std.secret::as_slice(&this->secret)));
        if (status != (0i32 as c_int32)) { throw failure(error_code::unavailable); }
    }
    return signature;
}

/* Whether `signature` is a valid signature of `message` by `public_key`; inputs of other
   lengths are not. */
bool verify(const u8[] public_key, const u8[] message, const u8[] signature) throws crypto_error {
    ready();
    if (len(public_key) != 32usize || len(signature) != 64usize) { return false; }
    unsafe {
        c_int32 status = r_std_crypto_native_verify(bytes_of(signature), bytes_of(message),
                                                    len(message) as c_size, bytes_of(public_key));
        return status == (0i32 as c_int32);
    }
}

/* ---- X25519 key exchange ---- */

/* R-SLIB-CRYPTO-0006: an X25519 key pair of RFC 7748. */
struct exchange_key {
    protected std.secret::buffer secret;
    u8[32] public_key;
};

exchange_key exchange_key::from_secret(const u8[] secret) throws std.alloc::alloc_error, crypto_error {
    ready();
    if (len(secret) != 32usize) { throw failure(error_code::invalid_length); }
    bytes copied = std.alloc::bytes(32usize, 0u8);
    for (usize index = 0usize; index < 32usize; index += 1usize) {
        copied[index] = secret[index];
    }
    std.secret::buffer kept = std.secret::from_bytes(move copied);
    u8[32] public_key = {};
    unsafe {
        c_int32 status = r_std_crypto_native_exchange_public(target_of(public_key[..]),
                                                             bytes_of(std.secret::as_slice(&kept)));
        if (status != (0i32 as c_int32)) { throw failure(error_code::unavailable); }
    }
    return exchange_key {.secret = move kept, .public_key = public_key};
}

exchange_key exchange_key::generate() throws std.alloc::alloc_error, crypto_error {
    bytes secret = random(32usize);
    std.secret::buffer kept = std.secret::from_bytes(move secret);
    return exchange_key::from_secret(std.secret::as_slice(&kept));
}

/* The 32-byte shared secret with the holder of `peer`; a peer key of small order gives the
   all-zero secret and is weak_key. */
std.secret::buffer exchange_key::shared(const exchange_key* this, const u8[] peer)
    throws std.alloc::alloc_error, crypto_error {
    ready();
    if (len(peer) != 32usize) { throw failure(error_code::invalid_length); }
    std.secret::buffer shared = std.secret::with_length(32usize);
    unsafe {
        c_int32 status = r_std_crypto_native_exchange_shared(target_of(std.secret::as_slice_mut(&shared)),
                                                             bytes_of(std.secret::as_slice(&this->secret)),
                                                             bytes_of(peer));
        if (status != (0i32 as c_int32)) { throw failure(error_code::weak_key); }
    }
    return move shared;
}

/* ---- HKDF ---- */

protected bytes hkdf(i32 hash, const u8[] salt, const u8[] key_material, const u8[] info, usize length)
    throws std.alloc::alloc_error, crypto_error {
    ready();
    if (length == 0usize || length > 255usize * ((hash / 8i32) as usize)) {
        throw failure(error_code::invalid_length);
    }
    bytes derived = std.alloc::bytes(length, 0u8);
    unsafe {
        c_int32 status = r_std_crypto_native_hkdf(hash as c_int32, bytes_of(salt), len(salt) as c_size,
                                                  bytes_of(key_material), len(key_material) as c_size,
                                                  bytes_of(info), len(info) as c_size,
                                                  target_of(derived.as_slice_mut()), length as c_size);
        if (status != (0i32 as c_int32)) { throw failure(error_code::invalid_length); }
    }
    return move derived;
}

/* R-SLIB-CRYPTO-0007: HKDF of RFC 5869 with SHA-256 or SHA-512; at most 255 hash lengths. */
bytes hkdf_sha256(const u8[] salt, const u8[] key_material, const u8[] info, usize length)
    throws std.alloc::alloc_error, crypto_error {
    return hkdf(256i32, salt, key_material, info, length);
}

bytes hkdf_sha512(const u8[] salt, const u8[] key_material, const u8[] info, usize length)
    throws std.alloc::alloc_error, crypto_error {
    return hkdf(512i32, salt, key_material, info, length);
}

/* ---- Argon2id ---- */

/* R-SLIB-CRYPTO-0008: the work of a password hash: passes over memory and memory in bytes. */
struct password_limits { u64 operations; usize memory; };

/* The limits libsodium calls interactive: 2 passes over 64 MiB. */
password_limits password_limits::interactive() {
    return password_limits {.operations = 2u64, .memory = 67108864usize};
}

/* A key of `length` bytes derived from `password` and a 16-byte salt with Argon2id v1.3 of
   RFC 9106. */
bytes argon2id(const u8[] password, const u8[] salt, password_limits limits, usize length)
    throws std.alloc::alloc_error, crypto_error {
    ready();
    if (len(salt) != 16usize || length < 16usize) { throw failure(error_code::invalid_length); }
    bytes derived = std.alloc::bytes(length, 0u8);
    unsafe {
        c_int32 status = r_std_crypto_native_argon2id(target_of(derived.as_slice_mut()), length as c_size,
                                                      bytes_of(password), len(password) as c_size,
                                                      bytes_of(salt), limits.operations as c_uint64,
                                                      limits.memory as c_size);
        if (status == (-1i32 as c_int32)) { throw failure(error_code::limits); }
        if (status != (0i32 as c_int32)) { throw failure(error_code::resource_exhausted); }
    }
    return move derived;
}

/* The encoded Argon2id hash of `password` with a random salt, as `$argon2id$v=19$...`. */
std.string::string password_hash(const u8[] password, password_limits limits)
    throws std.alloc::alloc_error, crypto_error {
    ready();
    u8[128] text = {};
    unsafe {
        c_int32 status = r_std_crypto_native_password_hash(target_of(text[..]), bytes_of(password),
                                                           len(password) as c_size,
                                                           limits.operations as c_uint64, limits.memory as c_size);
        if (status == (-1i32 as c_int32)) { throw failure(error_code::limits); }
        if (status != (0i32 as c_int32)) { throw failure(error_code::resource_exhausted); }
    }
    usize length = 0usize;
    while (length < 128usize && text[length] != 0u8) { length += 1usize; }
    try {
        return std.string::from_str(core::validate_utf8(text[0usize..length]));
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    throw failure(error_code::unavailable);
}

/* Whether `password` matches an encoded hash that password_hash made. */
bool password_verify(str hash, const u8[] password) throws crypto_error {
    ready();
    const u8[] text = hash;
    unsafe {
        c_int32 status = r_std_crypto_native_password_verify(bytes_of(text), len(text) as c_size,
                                                             bytes_of(password), len(password) as c_size);
        return status == (0i32 as c_int32);
    }
}

/* ---- BLAKE2b ---- */

/* R-SLIB-CRYPTO-0009: BLAKE2b of RFC 7693 with a digest of 16 to 64 bytes and a key of up to 64
   bytes, an empty key for plain hashing. */
bytes blake2b(const u8[] data, const u8[] key, usize length) throws std.alloc::alloc_error, crypto_error {
    ready();
    if (length < 16usize || length > 64usize || len(key) > 64usize) {
        throw failure(error_code::invalid_length);
    }
    bytes digest = std.alloc::bytes(length, 0u8);
    unsafe {
        c_int32 status = r_std_crypto_native_blake2b(target_of(digest.as_slice_mut()), length as c_size,
                                                     bytes_of(data), len(data) as c_size,
                                                     bytes_of(key), len(key) as c_size);
        if (status != (0i32 as c_int32)) { throw failure(error_code::invalid_length); }
    }
    return move digest;
}

/* BLAKE2b over data given in pieces. */
struct blake2b_state {
    protected u8[384] state;
    protected usize length;
};

blake2b_state blake2b_state::create(const u8[] key, usize length) throws crypto_error {
    ready();
    if (length < 16usize || length > 64usize || len(key) > 64usize) {
        throw failure(error_code::invalid_length);
    }
    u8[384] state = {};
    unsafe {
        c_int32 status = r_std_crypto_native_blake2b_start(target_of(state[..]), bytes_of(key),
                                                           len(key) as c_size, length as c_size);
        if (status != (0i32 as c_int32)) { throw failure(error_code::invalid_length); }
    }
    return blake2b_state {.state = state, .length = length};
}

void blake2b_state::update(blake2b_state* this, const u8[] data) {
    unsafe {
        c_int32 status = r_std_crypto_native_blake2b_update(target_of(this->state[..]), bytes_of(data),
                                                            len(data) as c_size);
        status as void;
    }
}

/* The digest; the state is erased and starts over with an empty key. */
bytes blake2b_state::finish(blake2b_state* this) throws std.alloc::alloc_error {
    bytes digest = std.alloc::bytes(this->length, 0u8);
    unsafe {
        c_int32 status = r_std_crypto_native_blake2b_finish(target_of(this->state[..]),
                                                            target_of(digest.as_slice_mut()),
                                                            this->length as c_size);
        status as void;
    }
    return move digest;
}

/* ---- Mbed TLS primitives ---- */

/* PSA Crypto starts on the first use of a primitive below; a failure to start is reported once
   more by every such operation. */
protected void pk_ready() throws crypto_error {
    unsafe {
        if (r_std_crypto_native_pk_ready() != (0i32 as c_int32)) { throw failure(error_code::unavailable); }
    }
}

/* The error of a negative status of the provider when the input is a key. */
protected crypto_error key_failure(i64 status) {
    if (status == -1i64) { return failure(error_code::invalid_key); }
    if (status == -3i64) { return failure(error_code::resource_exhausted); }
    return failure(error_code::unavailable);
}

/* A copy of a view in a new byte array. */
protected bytes copy_bytes(const u8[] data) throws std.alloc::alloc_error {
    bytes copied = std.bytes::with_capacity(len(data));
    std.bytes::append(&copied, data);
    return move copied;
}

/* A copy of a view in a new secret buffer. */
protected std.secret::buffer secret_copy(const u8[] data) throws std.alloc::alloc_error {
    std.secret::buffer kept = std.secret::with_length(len(data));
    try {
        std.bytes::copy(std.secret::as_slice_mut(&kept), data) as void;
    } catch (std.bytes::bytes_error rejected) {
        rejected as void;
    }
    return move kept;
}

/* ---- DER (ITU-T X.690) of the key formats ---- */

/* An item of a DER input: its tag and the bounds of its content; the next item starts at end. */
protected struct der_item { u8 tag; usize start; usize end; };

/* The item at `at`, which shall end at or before `limit`. Indefinite lengths, lengths that are
   not minimal or need more than 4 bytes, and tags of more than one byte are invalid_key. */
protected der_item der_at(const u8[] data, usize at, usize limit) throws crypto_error {
    if (limit > len(data) || at >= limit || limit - at < 2usize) { throw failure(error_code::invalid_key); }
    u8 tag = data[at];
    if (((tag as u32) & 31u32) == 31u32) { throw failure(error_code::invalid_key); }
    u32 first = data[at + 1usize] as u32;
    usize start = at + 2usize;
    usize length = first as usize;
    if (first >= 128u32) {
        usize count = (first - 128u32) as usize;
        if (count == 0usize || count > 4usize || limit - start < count) { throw failure(error_code::invalid_key); }
        length = 0usize;
        for (usize index = 0usize; index < count; index += 1usize) {
            length = (length << 8usize) | (data[start + index] as usize);
        }
        if (length < 128usize || data[start] == 0u8) { throw failure(error_code::invalid_key); }
        start += count;
    }
    if (length > limit - start) { throw failure(error_code::invalid_key); }
    return der_item {.tag = tag, .start = start, .end = start + length};
}

protected der_item der_expect(const u8[] data, usize at, usize limit, u8 tag) throws crypto_error {
    der_item item = der_at(data, at, limit);
    if (item.tag != tag) { throw failure(error_code::invalid_key); }
    return item;
}

/* The bytes of an unsigned big-endian number without its leading zero bytes. */
protected const u8[] unsigned_digits(const u8[] number) {
    usize first = 0usize;
    while (first < len(number) && number[first] == 0u8) { first += 1usize; }
    return number[first..len(number)];
}

/* The digits of a nonnegative INTEGER item. */
protected const u8[] der_unsigned_of(const u8[] data, der_item item) throws crypto_error {
    if (item.tag != 2u8 || item.end == item.start || data[item.start] >= 128u8) {
        throw failure(error_code::invalid_key);
    }
    return unsigned_digits(data[item.start..item.end]);
}

/* Appends an item with `tag` and `content` in the definite form. */
protected void der_put(bytes* out, u8 tag, const u8[] content) throws std.alloc::alloc_error {
    std.bytes::append_u8(out, tag);
    usize length = len(content);
    if (length < 128usize) {
        std.bytes::append_u8(out, length as u8);
    } else {
        usize count = 0usize;
        for (usize rest = length; rest > 0usize; rest = rest / 256usize) { count += 1usize; }
        std.bytes::append_u8(out, (128usize + count) as u8);
        for (usize index = count; index > 0usize; index -= 1usize) {
            std.bytes::append_u8(out, ((length >> (8usize * (index - 1usize))) & 255usize) as u8);
        }
    }
    std.bytes::append(out, content);
}

/* Appends the INTEGER of an unsigned big-endian number. */
protected void der_unsigned(bytes* out, const u8[] number) throws std.alloc::alloc_error {
    const u8[] digits = unsigned_digits(number);
    bytes content = std.bytes::with_capacity(len(digits) + 1usize);
    if (len(digits) == 0usize || digits[0usize] >= 128u8) { std.bytes::append_u8(&content, 0u8); }
    std.bytes::append(&content, digits);
    der_put(out, 2u8, content.as_slice());
}

/* Object identifiers of the key formats: id-ecPublicKey, prime256v1, secp384r1 (RFC 5480),
   rsaEncryption (RFC 8017), id-Ed25519 and id-X25519 (RFC 8410). */
protected const u8[7] OID_EC_PUBLIC_KEY = {42u8, 134u8, 72u8, 206u8, 61u8, 2u8, 1u8};
protected const u8[8] OID_P256 = {42u8, 134u8, 72u8, 206u8, 61u8, 3u8, 1u8, 7u8};
protected const u8[5] OID_P384 = {43u8, 129u8, 4u8, 0u8, 34u8};
protected const u8[9] OID_RSA = {42u8, 134u8, 72u8, 134u8, 247u8, 13u8, 1u8, 1u8, 1u8};
protected const u8[3] OID_ED25519 = {43u8, 101u8, 112u8};
protected const u8[3] OID_X25519 = {43u8, 101u8, 110u8};

/* ---- ECDSA ---- */

/* R-SLIB-CRYPTO-0009: the curves of ECDSA, each with its hash: P-256 (secp256r1) with SHA-256
   and P-384 (secp384r1) with SHA-384. */
enum curve { p256, p384 };

usize curve::scalar_length(curve this) {
    if (this == curve::p384) { return 48usize; }
    return 32usize;
}

usize curve::point_length(curve this) {
    return this.scalar_length() * 2usize + 1usize;
}

usize curve::signature_length(curve this) {
    return this.scalar_length() * 2usize;
}

protected c_int32 curve_code(curve which) {
    if (which == curve::p384) { return 1i32 as c_int32; }
    return 0i32 as c_int32;
}

protected curve curve_of_oid(const u8[] oid) throws crypto_error {
    if (std.bytes::equal(oid, OID_P256[..]) == true) { return curve::p256; }
    if (std.bytes::equal(oid, OID_P384[..]) == true) { return curve::p384; }
    throw failure(error_code::unsupported);
}

/* Appends the OBJECT IDENTIFIER of a curve. */
protected void put_curve_oid(bytes* out, curve which) throws std.alloc::alloc_error {
    if (which == curve::p384) {
        der_put(out, 6u8, OID_P384[..]);
    } else {
        der_put(out, 6u8, OID_P256[..]);
    }
}

protected i64 ecdsa_check_native(curve which, const u8[] point) {
    unsafe {
        c_int32 status = r_std_crypto_native_ecdsa_check(curve_code(which), bytes_of(point), len(point) as c_size);
        return status as i64;
    }
}

protected i64 ecdsa_verify_native(curve which, const u8[] point, const u8[] message, const u8[] signature) {
    unsafe {
        c_int32 status = r_std_crypto_native_ecdsa_verify(curve_code(which), bytes_of(point), len(point) as c_size,
                                                          bytes_of(message), len(message) as c_size,
                                                          bytes_of(signature), len(signature) as c_size);
        return status as i64;
    }
}

/* An ECDSA public key: its curve and the uncompressed SEC1 point 0x04 || X || Y. */
struct ecdsa_public_key {
    protected curve which;
    protected bytes encoded;
};

/* The key of an uncompressed point; a point of another length or form, or one not on the
   curve, is invalid_key. */
ecdsa_public_key ecdsa_public_key::from_point(curve which, const u8[] point)
    throws std.alloc::alloc_error, crypto_error {
    pk_ready();
    if (len(point) != which.point_length() || point[0usize] != 4u8) {
        throw failure(error_code::invalid_key);
    }
    i64 status = ecdsa_check_native(which, point);
    if (status != 0i64) { throw key_failure(status); }
    return ecdsa_public_key {.which = which, .encoded = copy_bytes(point)};
}

curve ecdsa_public_key::curve(const ecdsa_public_key* this) {
    return this->which;
}

const u8[] ecdsa_public_key::point(const ecdsa_public_key* this) {
    return this->encoded.as_slice();
}

/* Whether `signature`, r || s of the scalar size each, is an ECDSA signature of `message` with
   the hash of the curve; a signature of another length is not. */
bool ecdsa_public_key::verify(const ecdsa_public_key* this, const u8[] message, const u8[] signature)
    throws crypto_error {
    pk_ready();
    i64 status = ecdsa_verify_native(this->which, this->encoded.as_slice(), message, signature);
    if (status == 0i64) { return true; }
    if (status == -2i64) { return false; }
    throw key_failure(status);
}

/* An ECDSA key pair: the private scalar in a secret buffer and the public point. */
struct ecdsa_key {
    protected curve which;
    protected std.secret::buffer scalar;
    protected bytes encoded;
};

protected i64 ecdsa_generate_native(curve which, u8[] scalar, u8[] point) {
    unsafe {
        c_int32 status = r_std_crypto_native_ecdsa_generate(curve_code(which), target_of(scalar), target_of(point));
        return status as i64;
    }
}

protected i64 ecdsa_public_native(curve which, const u8[] scalar, u8[] point) {
    unsafe {
        c_int32 status = r_std_crypto_native_ecdsa_public(curve_code(which), bytes_of(scalar), target_of(point));
        return status as i64;
    }
}

protected i64 ecdsa_sign_native(curve which, const u8[] scalar, const u8[] message, u8[] signature) {
    unsafe {
        c_int32 status = r_std_crypto_native_ecdsa_sign(curve_code(which), bytes_of(scalar), bytes_of(message),
                                                        len(message) as c_size, target_of(signature));
        return status as i64;
    }
}

ecdsa_key ecdsa_key::generate(curve which) throws std.alloc::alloc_error, crypto_error {
    pk_ready();
    std.secret::buffer scalar = std.secret::with_length(which.scalar_length());
    bytes point = std.alloc::bytes(which.point_length(), 0u8);
    i64 status = ecdsa_generate_native(which, std.secret::as_slice_mut(&scalar), point.as_slice_mut());
    if (status != 0i64) { throw key_failure(status); }
    return ecdsa_key {.which = which, .scalar = move scalar, .encoded = move point};
}

/* The key pair of a big-endian private scalar of the scalar size, or shorter with its leading
   zero bytes left out; zero and scalars not below the order of the curve are invalid_key. */
ecdsa_key ecdsa_key::from_scalar(curve which, const u8[] scalar) throws std.alloc::alloc_error, crypto_error {
    pk_ready();
    usize size = which.scalar_length();
    if (len(scalar) > size || len(scalar) == 0usize) { throw failure(error_code::invalid_key); }
    std.secret::buffer kept = std.secret::with_length(size);
    try {
        u8[] target = std.secret::as_slice_mut(&kept);
        std.bytes::copy(target[(size - len(scalar))..size], scalar) as void;
    } catch (std.bytes::bytes_error rejected) {
        rejected as void;
    }
    bytes point = std.alloc::bytes(which.point_length(), 0u8);
    i64 status = ecdsa_public_native(which, std.secret::as_slice(&kept), point.as_slice_mut());
    if (status != 0i64) { throw key_failure(status); }
    return ecdsa_key {.which = which, .scalar = move kept, .encoded = move point};
}

curve ecdsa_key::curve(const ecdsa_key* this) {
    return this->which;
}

ecdsa_public_key ecdsa_key::public_key(const ecdsa_key* this) throws std.alloc::alloc_error {
    return ecdsa_public_key {.which = this->which, .encoded = copy_bytes(this->encoded.as_slice())};
}

/* The deterministic signature of RFC 6979 of `message`, r || s of the scalar size each. */
bytes ecdsa_key::sign(const ecdsa_key* this, const u8[] message) throws std.alloc::alloc_error, crypto_error {
    pk_ready();
    bytes signature = std.alloc::bytes(this->which.signature_length(), 0u8);
    i64 status = ecdsa_sign_native(this->which, std.secret::as_slice(&this->scalar), message,
                                   signature.as_slice_mut());
    if (status != 0i64) { throw key_failure(status); }
    return move signature;
}

/* ---- RSA ---- */

/* R-SLIB-CRYPTO-0010: the signature schemes of RSA (RFC 8017): PKCS#1 v1.5 and PSS with MGF1 of
   the same hash and a salt of the hash length. */
enum rsa_scheme { pkcs1_sha256, pkcs1_sha384, pkcs1_sha512, pss_sha256, pss_sha384, pss_sha512 };

protected c_int32 scheme_code(rsa_scheme scheme) {
    switch (scheme) {
    case rsa_scheme::pkcs1_sha256: return 0i32 as c_int32;
    case rsa_scheme::pkcs1_sha384: return 1i32 as c_int32;
    case rsa_scheme::pkcs1_sha512: return 2i32 as c_int32;
    case rsa_scheme::pss_sha256: return 3i32 as c_int32;
    case rsa_scheme::pss_sha384: return 4i32 as c_int32;
    case rsa_scheme::pss_sha512: return 5i32 as c_int32;
    }
}

protected i64 rsa_check_public_native(const u8[] key) {
    unsafe {
        c_int32 status = r_std_crypto_native_rsa_check_public(bytes_of(key), len(key) as c_size);
        return status as i64;
    }
}

protected i64 rsa_check_private_native(const u8[] key) {
    unsafe {
        c_int32 status = r_std_crypto_native_rsa_check_private(bytes_of(key), len(key) as c_size);
        return status as i64;
    }
}

protected i64 rsa_generate_native(usize bits, u8[] target) {
    unsafe {
        c_int32 status = r_std_crypto_native_rsa_generate(bits as c_uint32, target_of(target), len(target) as c_size);
        return status as i64;
    }
}

protected i64 rsa_sign_native(rsa_scheme scheme, const u8[] key, const u8[] message, u8[] signature) {
    unsafe {
        c_int32 status = r_std_crypto_native_rsa_sign(scheme_code(scheme), bytes_of(key), len(key) as c_size,
                                                      bytes_of(message), len(message) as c_size,
                                                      target_of(signature), len(signature) as c_size);
        return status as i64;
    }
}

protected i64 rsa_verify_native(rsa_scheme scheme, const u8[] key, const u8[] message, const u8[] signature) {
    unsafe {
        c_int32 status = r_std_crypto_native_rsa_verify(scheme_code(scheme), bytes_of(key), len(key) as c_size,
                                                        bytes_of(message), len(message) as c_size,
                                                        bytes_of(signature), len(signature) as c_size);
        return status as i64;
    }
}

/* An RSA public key: modulus and public exponent as big-endian unsigned numbers without leading
   zero bytes, and their PKCS#1 RSAPublicKey. */
struct rsa_public_key {
    protected bytes modulus_bytes;
    protected bytes exponent_bytes;
    protected bytes der;
    protected usize size;
};

protected rsa_public_key public_of(const u8[] modulus, const u8[] exponent)
    throws std.alloc::alloc_error, crypto_error {
    pk_ready();
    const u8[] n = unsigned_digits(modulus);
    const u8[] e = unsigned_digits(exponent);
    if (len(n) == 0usize || len(e) == 0usize) { throw failure(error_code::invalid_key); }
    bytes content = std.bytes::with_capacity(len(n) + len(e) + 16usize);
    der_unsigned(&content, n);
    der_unsigned(&content, e);
    bytes encoded = std.bytes::with_capacity(len(content) + 8usize);
    der_put(&encoded, 48u8, content.as_slice());
    i64 bits = rsa_check_public_native(encoded.as_slice());
    if (bits == -3i64) { throw failure(error_code::resource_exhausted); }
    if (bits < 2048i64) { throw failure(error_code::invalid_key); }
    return rsa_public_key {.modulus_bytes = copy_bytes(n), .exponent_bytes = copy_bytes(e),
                           .der = move encoded, .size = bits as usize};
}

/* The key of a modulus and a public exponent; leading zero bytes are ignored, and a modulus of
   fewer than 2048 or more than 4096 bits, or a key the provider refuses, is invalid_key. */
rsa_public_key rsa_public_key::from_components(const u8[] modulus, const u8[] exponent)
    throws std.alloc::alloc_error, crypto_error {
    return public_of(modulus, exponent);
}

const u8[] rsa_public_key::modulus(const rsa_public_key* this) {
    return this->modulus_bytes.as_slice();
}

const u8[] rsa_public_key::exponent(const rsa_public_key* this) {
    return this->exponent_bytes.as_slice();
}

usize rsa_public_key::bits(const rsa_public_key* this) {
    return this->size;
}

/* Whether `signature` is a signature of `message` under `scheme`; a signature of another
   length than the modulus is not. */
bool rsa_public_key::verify(const rsa_public_key* this, rsa_scheme scheme, const u8[] message,
                            const u8[] signature) throws crypto_error {
    pk_ready();
    i64 status = rsa_verify_native(scheme, this->der.as_slice(), message, signature);
    if (status == 0i64) { return true; }
    if (status == -2i64) { return false; }
    throw key_failure(status);
}

protected rsa_public_key copy_public(const rsa_public_key* key) throws std.alloc::alloc_error {
    return rsa_public_key {.modulus_bytes = copy_bytes(key->modulus_bytes.as_slice()),
                           .exponent_bytes = copy_bytes(key->exponent_bytes.as_slice()),
                           .der = copy_bytes(key->der.as_slice()), .size = key->size};
}

/* An RSA private key: its PKCS#1 RSAPrivateKey in a secret buffer and its public key. */
struct rsa_key {
    protected std.secret::buffer der;
    protected rsa_public_key public_part;
};

/* The public key that a PKCS#1 RSAPrivateKey holds; only two-prime keys (version 0) are read. */
protected rsa_public_key public_from_private(const u8[] data) throws std.alloc::alloc_error, crypto_error {
    der_item outer = der_expect(data, 0usize, len(data), 48u8);
    if (outer.end != len(data)) { throw failure(error_code::invalid_key); }
    der_item version = der_expect(data, outer.start, outer.end, 2u8);
    if (version.end != version.start + 1usize || data[version.start] != 0u8) {
        throw failure(error_code::unsupported);
    }
    der_item modulus = der_expect(data, version.end, outer.end, 2u8);
    der_item exponent = der_expect(data, modulus.end, outer.end, 2u8);
    return public_of(der_unsigned_of(data, modulus), der_unsigned_of(data, exponent));
}

/* The key of a PKCS#1 RSAPrivateKey that the provider accepts. */
protected rsa_key rsa_of(std.secret::buffer der) throws std.alloc::alloc_error, crypto_error {
    pk_ready();
    i64 bits = rsa_check_private_native(std.secret::as_slice(&der));
    if (bits == -3i64) { throw failure(error_code::resource_exhausted); }
    if (bits < 2048i64) { throw failure(error_code::invalid_key); }
    rsa_public_key public_part = public_from_private(std.secret::as_slice(&der));
    return rsa_key {.der = move der, .public_part = move public_part};
}

/* A new key with a modulus of `bits` bits, 2048 to 4096 in steps of 8, and the exponent 65537. */
rsa_key rsa_key::generate(usize bits) throws std.alloc::alloc_error, crypto_error {
    pk_ready();
    if (bits < 2048usize || bits > 4096usize || bits % 8usize != 0usize) {
        throw failure(error_code::invalid_length);
    }
    std.secret::buffer staged = std.secret::with_length(bits + 256usize);
    i64 length = rsa_generate_native(bits, std.secret::as_slice_mut(&staged));
    if (length <= 0i64) { throw key_failure(length); }
    const u8[] generated = std.secret::as_slice(&staged);
    std.secret::buffer der = secret_copy(generated[0usize..(length as usize)]);
    drop staged;
    return rsa_of(move der);
}

usize rsa_key::bits(const rsa_key* this) {
    return this->public_part.size;
}

rsa_public_key rsa_key::public_key(const rsa_key* this) throws std.alloc::alloc_error {
    return copy_public(&this->public_part);
}

/* The signature of `message` under `scheme`, of the modulus size. */
bytes rsa_key::sign(const rsa_key* this, rsa_scheme scheme, const u8[] message)
    throws std.alloc::alloc_error, crypto_error {
    pk_ready();
    usize size = this->public_part.size / 8usize;
    bytes signature = std.alloc::bytes(size, 0u8);
    i64 written = rsa_sign_native(scheme, std.secret::as_slice(&this->der), message, signature.as_slice_mut());
    if (written < 0i64) { throw key_failure(written); }
    if ((written as usize) != size) { throw failure(error_code::unavailable); }
    return move signature;
}

/* ---- AES-CBC ---- */

protected i64 cbc_native(bool encrypt, const u8[] key, const u8[] iv, const u8[] input, u8[] target) {
    i32 mode = 0i32;
    if (encrypt == true) { mode = 1i32; }
    unsafe {
        c_int64 status = r_std_crypto_native_cbc(mode as c_int32, bytes_of(key), len(key) as c_size, bytes_of(iv),
                                                 bytes_of(input), len(input) as c_size, target_of(target),
                                                 len(target) as c_size);
        return status as i64;
    }
}

protected bytes cbc(bool encrypt, const u8[] key, const u8[] iv, const u8[] input)
    throws std.alloc::alloc_error, crypto_error {
    pk_ready();
    if ((len(key) != 16usize && len(key) != 24usize && len(key) != 32usize) || len(iv) != 16usize) {
        throw failure(error_code::invalid_length);
    }
    if (encrypt == false && (len(input) == 0usize || len(input) % 16usize != 0usize)) {
        throw failure(error_code::invalid_length);
    }
    std.secret::buffer output = std.secret::with_length(len(input) + 16usize);
    i64 written = cbc_native(encrypt, key, iv, input, std.secret::as_slice_mut(&output));
    if (written == -2i64) { throw failure(error_code::invalid_padding); }
    if (written == -1i64) { throw failure(error_code::invalid_length); }
    if (written == -3i64) { throw failure(error_code::resource_exhausted); }
    if (written < 0i64) { throw failure(error_code::unavailable); }
    const u8[] result = std.secret::as_slice(&output);
    return copy_bytes(result[0usize..(written as usize)]);
}

/* R-SLIB-CRYPTO-0011: AES in CBC mode with PKCS#7 padding: the ciphertext of `plaintext` under a
   16-, 24- or 32-byte key and a 16-byte IV, 1 to 16 bytes longer than the plaintext. CBC does
   not authenticate; it serves protocols that require it. */
bytes cbc_encrypt(const u8[] key, const u8[] iv, const u8[] plaintext) throws std.alloc::alloc_error, crypto_error {
    return cbc(true, key, iv, plaintext);
}

/* The plaintext of a CBC ciphertext whose length is a positive multiple of 16; a padding that
   does not check is invalid_padding. */
bytes cbc_decrypt(const u8[] key, const u8[] iv, const u8[] ciphertext) throws std.alloc::alloc_error, crypto_error {
    return cbc(false, key, iv, ciphertext);
}

/* ---- Key formats: PKCS#8, SEC1, PKCS#1, SPKI and PEM ---- */

/* R-SLIB-CRYPTO-0012: a private key of one of the kinds of the module. */
enum private_key {
    ecdsa(ecdsa_key),
    rsa(rsa_key),
    ed25519(signing_key),
    x25519(exchange_key),
};

/* R-SLIB-CRYPTO-0012: a public key of one of the kinds of the module. */
enum public_key {
    ecdsa(ecdsa_public_key),
    rsa(rsa_public_key),
    ed25519(u8[32]),
    x25519(u8[32]),
};

/* The ECDSA key of a SEC1 ECPrivateKey (RFC 5915). Inside PKCS#8 the curve is known from the
   algorithm; alone, the key names its curve. */
protected ecdsa_key ec_private_of(const u8[] data, bool known, curve given)
    throws std.alloc::alloc_error, crypto_error {
    der_item outer = der_expect(data, 0usize, len(data), 48u8);
    if (outer.end != len(data)) { throw failure(error_code::invalid_key); }
    der_item version = der_expect(data, outer.start, outer.end, 2u8);
    if (version.end != version.start + 1usize || data[version.start] != 1u8) {
        throw failure(error_code::invalid_key);
    }
    der_item secret = der_expect(data, version.end, outer.end, 4u8);
    bool named = false;
    curve which = given;
    if (secret.end < outer.end) {
        der_item next = der_at(data, secret.end, outer.end);
        if (next.tag == 160u8) {
            der_item oid = der_expect(data, next.start, next.end, 6u8);
            curve stated = curve_of_oid(data[oid.start..oid.end]);
            if (known == true && stated != given) { throw failure(error_code::invalid_key); }
            which = stated;
            named = true;
        }
    }
    if (known == false && named == false) { throw failure(error_code::unsupported); }
    return ecdsa_key::from_scalar(which, unsigned_digits(data[secret.start..secret.end]));
}

/* The 32-byte key of the CurvePrivateKey of RFC 8410 inside a PKCS#8 key. */
protected const u8[] curve_private_of(const u8[] inner) throws crypto_error {
    der_item seed = der_expect(inner, 0usize, len(inner), 4u8);
    if (seed.end != len(inner) || seed.end - seed.start != 32usize) { throw failure(error_code::invalid_key); }
    return inner[seed.start..seed.end];
}

/* The key of a PKCS#8 PrivateKeyInfo (RFC 5208, RFC 5958) without encryption. */
protected private_key pkcs8_of(const u8[] data) throws std.alloc::alloc_error, crypto_error {
    der_item outer = der_expect(data, 0usize, len(data), 48u8);
    if (outer.end != len(data)) { throw failure(error_code::invalid_key); }
    der_item version = der_expect(data, outer.start, outer.end, 2u8);
    if (version.end != version.start + 1usize || data[version.start] > 1u8) {
        throw failure(error_code::invalid_key);
    }
    der_item algorithm = der_expect(data, version.end, outer.end, 48u8);
    der_item oid = der_expect(data, algorithm.start, algorithm.end, 6u8);
    der_item key = der_expect(data, algorithm.end, outer.end, 4u8);
    const u8[] name = data[oid.start..oid.end];
    const u8[] inner = data[key.start..key.end];
    if (std.bytes::equal(name, OID_EC_PUBLIC_KEY[..]) == true) {
        der_item parameter = der_expect(data, oid.end, algorithm.end, 6u8);
        curve which = curve_of_oid(data[parameter.start..parameter.end]);
        return private_key::ecdsa(ec_private_of(inner, true, which));
    }
    if (std.bytes::equal(name, OID_RSA[..]) == true) {
        return private_key::rsa(rsa_of(secret_copy(inner)));
    }
    if (std.bytes::equal(name, OID_ED25519[..]) == true) {
        return private_key::ed25519(signing_key::from_seed(curve_private_of(inner)));
    }
    if (std.bytes::equal(name, OID_X25519[..]) == true) {
        return private_key::x25519(exchange_key::from_secret(curve_private_of(inner)));
    }
    throw failure(error_code::unsupported);
}

/* The key of a DER PKCS#8 PrivateKeyInfo, SEC1 ECPrivateKey or PKCS#1 RSAPrivateKey, told apart
   by their structure. */
private_key private_key::from_der(const u8[] der) throws std.alloc::alloc_error, crypto_error {
    der_item outer = der_expect(der, 0usize, len(der), 48u8);
    if (outer.end != len(der)) { throw failure(error_code::invalid_key); }
    der_item version = der_expect(der, outer.start, outer.end, 2u8);
    der_item second = der_at(der, version.end, outer.end);
    if (second.tag == 48u8) { return pkcs8_of(der); }
    if (second.tag == 4u8) { return private_key::ecdsa(ec_private_of(der, false, curve::p256)); }
    if (second.tag == 2u8) { return private_key::rsa(rsa_of(secret_copy(der))); }
    throw failure(error_code::invalid_key);
}

/* A block of a PEM text (RFC 7468): the bounds of its label, its content and where the text
   after it starts; a block with header lines (RFC 1421 encryption) has no content. */
protected struct pem_block {
    usize label_start;
    usize label_end;
    bool headers;
    std.secret::buffer content;
    usize next;
};

/* The first index at or after `from` where `pattern` starts, or the length of `text`. */
protected usize find_from(const u8[] text, usize from, const u8[] pattern) {
    if (from >= len(text)) { return len(text); }
    switch (std.bytes::find_slice(text[from..len(text)], pattern)) {
    case variant o::some(found): return from + *found;
    case variant o::none: return len(text);
    }
}

protected bool base64_letter(u8 c) {
    return (c >= 65u8 && c <= 90u8) || (c >= 97u8 && c <= 122u8) || (c >= 48u8 && c <= 57u8) || c == 43u8 ||
           c == 47u8 || c == 61u8;
}

protected std.secret::buffer base64_content(const u8[] digits) throws std.alloc::alloc_error, crypto_error {
    try {
        str text = core::validate_utf8(digits);
        return std.secret::from_bytes(std.encoding::decode_base64(text));
    } catch (core::utf8_error rejected) {
        rejected as void;
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_key);
}

/* The next PEM block at or after `from`; a block without its end line is invalid_key. */
protected o<pem_block> pem_next(const u8[] text, usize from) throws std.alloc::alloc_error, crypto_error {
    str begin_text = "-----BEGIN ";
    const u8[] begin = begin_text;
    str end_text = "-----END ";
    const u8[] end_marker = end_text;
    str dashes_text = "-----";
    const u8[] dashes = dashes_text;
    usize start = find_from(text, from, begin);
    if (start >= len(text)) { return o::none; }
    usize label_start = start + len(begin);
    usize label_end = find_from(text, label_start, dashes);
    usize body = label_end + len(dashes);
    usize stop = find_from(text, body, end_marker);
    usize end_label = stop + len(end_marker);
    usize end_label_end = find_from(text, end_label, dashes);
    if (end_label_end >= len(text)) { throw failure(error_code::invalid_key); }
    if (std.bytes::equal(text[end_label..end_label_end], text[label_start..label_end]) == false) {
        throw failure(error_code::invalid_key);
    }
    bytes digits = std.bytes::with_capacity(stop - body);
    bool headers = false;
    for (usize index = body; index < stop; index += 1usize) {
        u8 c = text[index];
        if (c == 58u8) { headers = true; }
        if (base64_letter(c) == true) { std.bytes::append_u8(&digits, c); }
    }
    usize next = end_label_end + len(dashes);
    if (headers == true) {
        std.secret::zeroize(digits.as_slice_mut());
        return o::some(pem_block {.label_start = label_start, .label_end = label_end, .headers = true,
                                  .content = std.secret::with_length(0usize), .next = next});
    }
    std.secret::buffer content = base64_content(digits.as_slice());
    std.secret::zeroize(digits.as_slice_mut());
    return o::some(pem_block {.label_start = label_start, .label_end = label_end, .headers = false,
                              .content = move content, .next = next});
}

protected bool label_is(const u8[] text, const pem_block* block, str name) {
    const u8[] wanted = name;
    return std.bytes::equal(text[block->label_start..block->label_end], wanted);
}

/* What a PEM block holds for the readers below: 0 another label, 1 PKCS#8, 2 SEC1, 3 PKCS#1
   private, 4 SPKI, 5 PKCS#1 public, 6 an encrypted private key. */
protected u32 pem_kind(const u8[] text, const pem_block* block) {
    if (label_is(text, block, "ENCRYPTED PRIVATE KEY") == true) { return 6u32; }
    u32 kind = 0u32;
    if (label_is(text, block, "PRIVATE KEY") == true) { kind = 1u32; }
    if (label_is(text, block, "EC PRIVATE KEY") == true) { kind = 2u32; }
    if (label_is(text, block, "RSA PRIVATE KEY") == true) { kind = 3u32; }
    if (label_is(text, block, "PUBLIC KEY") == true) { kind = 4u32; }
    if (label_is(text, block, "RSA PUBLIC KEY") == true) { kind = 5u32; }
    if (kind != 0u32 && kind < 4u32 && block->headers == true) { return 6u32; }
    return kind;
}

/* The key of the first block of a PEM text labelled PRIVATE KEY (PKCS#8), EC PRIVATE KEY (SEC1)
   or RSA PRIVATE KEY (PKCS#1); blocks with other labels, such as EC PARAMETERS, are skipped. An
   encrypted key is unsupported, a text without such a block invalid_key. */
private_key private_key::from_pem(const u8[] text) throws std.alloc::alloc_error, crypto_error {
    usize from = 0usize;
    while (from < len(text)) {
        o<pem_block> found = pem_next(text, from);
        switch (move found) {
        case variant o::none: throw failure(error_code::invalid_key);
        case variant o::some(move block):
            u32 kind = pem_kind(text, &block);
            if (kind == 6u32) { throw failure(error_code::unsupported); }
            if (kind == 1u32) { return pkcs8_of(std.secret::as_slice(&block.content)); }
            if (kind == 2u32) {
                return private_key::ecdsa(ec_private_of(std.secret::as_slice(&block.content), false, curve::p256));
            }
            if (kind == 3u32) { return private_key::rsa(rsa_of(secret_copy(std.secret::as_slice(&block.content)))); }
            from = block.next;
        }
    }
    throw failure(error_code::invalid_key);
}

/* Appends the AlgorithmIdentifier of a key kind. */
protected void put_algorithm(bytes* out, const u8[] oid, u32 parameter, curve which) throws std.alloc::alloc_error {
    bytes content = std.bytes::with_capacity(24usize);
    der_put(&content, 6u8, oid);
    if (parameter == 1u32) { put_curve_oid(&content, which); }
    if (parameter == 2u32) { der_put(&content, 5u8, OID_RSA[0usize..0usize]); }
    der_put(out, 48u8, content.as_slice());
}

/* Appends a PKCS#8 PrivateKeyInfo of version 0. */
protected void put_pkcs8(bytes* out, const u8[] oid, u32 parameter, curve which, const u8[] inner)
    throws std.alloc::alloc_error {
    bytes content = std.bytes::with_capacity(len(inner) + 32usize);
    u8[1] zero = {0u8};
    der_put(&content, 2u8, zero[..]);
    put_algorithm(&content, oid, parameter, which);
    der_put(&content, 4u8, inner);
    der_put(out, 48u8, content.as_slice());
    std.secret::zeroize(content.as_slice_mut());
}

/* The SEC1 ECPrivateKey of a key as PKCS#8 holds it: version 1, the scalar and the public point,
   without the curve, which the algorithm names. */
protected void put_ec_private(bytes* out, const ecdsa_key* key) throws std.alloc::alloc_error {
    bytes content = std.bytes::with_capacity(160usize);
    u8[1] one = {1u8};
    der_put(&content, 2u8, one[..]);
    der_put(&content, 4u8, std.secret::as_slice(&key->scalar));
    bytes bits = std.bytes::with_capacity(len(key->encoded) + 1usize);
    std.bytes::append_u8(&bits, 0u8);
    std.bytes::append(&bits, key->encoded.as_slice());
    bytes wrapped = std.bytes::with_capacity(len(bits) + 4usize);
    der_put(&wrapped, 3u8, bits.as_slice());
    der_put(&content, 161u8, wrapped.as_slice());
    der_put(out, 48u8, content.as_slice());
    std.secret::zeroize(content.as_slice_mut());
}

/* The DER of the PKCS#8 PrivateKeyInfo of the key, in a secret buffer; it reads back with
   from_der and matches what OpenSSL writes for the same key. */
std.secret::buffer private_key::to_der(const private_key* this) throws std.alloc::alloc_error {
    bytes out = std.bytes::with_capacity(2560usize);
    switch (*this) {
    case variant private_key::ecdsa(key):
        bytes inner = std.bytes::with_capacity(160usize);
        put_ec_private(&inner, key);
        put_pkcs8(&out, OID_EC_PUBLIC_KEY[..], 1u32, key->which, inner.as_slice());
        std.secret::zeroize(inner.as_slice_mut());
    case variant private_key::rsa(key):
        put_pkcs8(&out, OID_RSA[..], 2u32, curve::p256, std.secret::as_slice(&key->der));
    case variant private_key::ed25519(key):
        const u8[] secret = std.secret::as_slice(&key->secret);
        bytes inner = std.bytes::with_capacity(34usize);
        der_put(&inner, 4u8, secret[0usize..32usize]);
        put_pkcs8(&out, OID_ED25519[..], 0u32, curve::p256, inner.as_slice());
        std.secret::zeroize(inner.as_slice_mut());
    case variant private_key::x25519(key):
        bytes inner = std.bytes::with_capacity(34usize);
        der_put(&inner, 4u8, std.secret::as_slice(&key->secret));
        put_pkcs8(&out, OID_X25519[..], 0u32, curve::p256, inner.as_slice());
        std.secret::zeroize(inner.as_slice_mut());
    }
    return std.secret::from_bytes(move out);
}

/* Appends the PEM text of DER content under a label: 64 base64 characters per line. */
protected void put_pem(bytes* out, str label, const u8[] der) throws std.alloc::alloc_error {
    str begin_text = "-----BEGIN ";
    const u8[] begin = begin_text;
    str end_text = "-----END ";
    const u8[] end_marker = end_text;
    str dashes_text = "-----\n";
    const u8[] dashes = dashes_text;
    const u8[] name = label;
    std.bytes::append(out, begin);
    std.bytes::append(out, name);
    std.bytes::append(out, dashes);
    std.string::string encoded = std.encoding::encode_base64(der);
    const u8[] digits = encoded;
    for (usize at = 0usize; at < len(digits); at += 64usize) {
        usize stop = at + 64usize;
        if (stop > len(digits)) { stop = len(digits); }
        std.bytes::append(out, digits[at..stop]);
        std.bytes::append_u8(out, 10u8);
    }
    std.bytes::append(out, end_marker);
    std.bytes::append(out, name);
    std.bytes::append(out, dashes);
}

/* The PEM text of the PKCS#8 key under the label PRIVATE KEY, in a secret buffer. */
std.secret::buffer private_key::to_pem(const private_key* this) throws std.alloc::alloc_error {
    std.secret::buffer der = this->to_der();
    bytes out = std.bytes::with_capacity(std.secret::len(&der) * 2usize + 64usize);
    put_pem(&out, "PRIVATE KEY", std.secret::as_slice(&der));
    return std.secret::from_bytes(move out);
}

/* The public key of a private key. */
public_key private_key::public_key(const private_key* this) throws std.alloc::alloc_error {
    switch (*this) {
    case variant private_key::ecdsa(key): return public_key::ecdsa(key->public_key());
    case variant private_key::rsa(key): return public_key::rsa(key->public_key());
    case variant private_key::ed25519(key): return public_key::ed25519(key->public_key);
    case variant private_key::x25519(key): return public_key::x25519(key->public_key);
    }
}

/* The public key of a PKCS#1 RSAPublicKey. */
protected public_key rsa_public_of(const u8[] data) throws std.alloc::alloc_error, crypto_error {
    der_item outer = der_expect(data, 0usize, len(data), 48u8);
    if (outer.end != len(data)) { throw failure(error_code::invalid_key); }
    der_item modulus = der_expect(data, outer.start, outer.end, 2u8);
    der_item exponent = der_expect(data, modulus.end, outer.end, 2u8);
    if (exponent.end != outer.end) { throw failure(error_code::invalid_key); }
    return public_key::rsa(public_of(der_unsigned_of(data, modulus), der_unsigned_of(data, exponent)));
}

/* The 32-byte key of an Ed25519 or X25519 SubjectPublicKeyInfo. */
protected u8[32] curve_public_of(const u8[] bits) throws crypto_error {
    if (len(bits) != 32usize) { throw failure(error_code::invalid_key); }
    u8[32] key = {};
    for (usize index = 0usize; index < 32usize; index += 1usize) {
        key[index] = bits[index];
    }
    return key;
}

/* The key of a SubjectPublicKeyInfo (RFC 5280, RFC 5480, RFC 8410). */
protected public_key spki_of(const u8[] data) throws std.alloc::alloc_error, crypto_error {
    der_item outer = der_expect(data, 0usize, len(data), 48u8);
    if (outer.end != len(data)) { throw failure(error_code::invalid_key); }
    der_item algorithm = der_expect(data, outer.start, outer.end, 48u8);
    der_item oid = der_expect(data, algorithm.start, algorithm.end, 6u8);
    der_item key = der_expect(data, algorithm.end, outer.end, 3u8);
    if (key.end != outer.end || key.end == key.start || data[key.start] != 0u8) {
        throw failure(error_code::invalid_key);
    }
    const u8[] name = data[oid.start..oid.end];
    const u8[] bits = data[(key.start + 1usize)..key.end];
    if (std.bytes::equal(name, OID_EC_PUBLIC_KEY[..]) == true) {
        der_item parameter = der_expect(data, oid.end, algorithm.end, 6u8);
        curve which = curve_of_oid(data[parameter.start..parameter.end]);
        if (len(bits) == 0usize || bits[0usize] != 4u8) { throw failure(error_code::unsupported); }
        return public_key::ecdsa(ecdsa_public_key::from_point(which, bits));
    }
    if (std.bytes::equal(name, OID_RSA[..]) == true) { return rsa_public_of(bits); }
    if (std.bytes::equal(name, OID_ED25519[..]) == true) { return public_key::ed25519(curve_public_of(bits)); }
    if (std.bytes::equal(name, OID_X25519[..]) == true) { return public_key::x25519(curve_public_of(bits)); }
    throw failure(error_code::unsupported);
}

/* The key of a DER SubjectPublicKeyInfo or PKCS#1 RSAPublicKey, told apart by their structure.
   Compressed EC points are unsupported. */
public_key public_key::from_der(const u8[] der) throws std.alloc::alloc_error, crypto_error {
    der_item outer = der_expect(der, 0usize, len(der), 48u8);
    der_item first = der_at(der, outer.start, outer.end);
    if (first.tag == 48u8) { return spki_of(der); }
    return rsa_public_of(der);
}

/* The key of the first block of a PEM text labelled PUBLIC KEY (SPKI) or RSA PUBLIC KEY (PKCS#1);
   blocks with other labels are skipped. */
public_key public_key::from_pem(const u8[] text) throws std.alloc::alloc_error, crypto_error {
    usize from = 0usize;
    while (from < len(text)) {
        o<pem_block> found = pem_next(text, from);
        switch (move found) {
        case variant o::none: throw failure(error_code::invalid_key);
        case variant o::some(move block):
            u32 kind = pem_kind(text, &block);
            if (kind == 4u32) { return spki_of(std.secret::as_slice(&block.content)); }
            if (kind == 5u32) { return rsa_public_of(std.secret::as_slice(&block.content)); }
            from = block.next;
        }
    }
    throw failure(error_code::invalid_key);
}

/* Appends a SubjectPublicKeyInfo. */
protected void put_spki(bytes* out, const u8[] oid, u32 parameter, curve which, const u8[] key)
    throws std.alloc::alloc_error {
    bytes content = std.bytes::with_capacity(len(key) + 32usize);
    put_algorithm(&content, oid, parameter, which);
    bytes bits = std.bytes::with_capacity(len(key) + 1usize);
    std.bytes::append_u8(&bits, 0u8);
    std.bytes::append(&bits, key);
    der_put(&content, 3u8, bits.as_slice());
    der_put(out, 48u8, content.as_slice());
}

/* The DER of the SubjectPublicKeyInfo of the key. */
bytes public_key::to_der(const public_key* this) throws std.alloc::alloc_error {
    bytes out = std.bytes::with_capacity(600usize);
    switch (*this) {
    case variant public_key::ecdsa(key): put_spki(&out, OID_EC_PUBLIC_KEY[..], 1u32, key->which, key->encoded.as_slice());
    case variant public_key::rsa(key): put_spki(&out, OID_RSA[..], 2u32, curve::p256, key->der.as_slice());
    case variant public_key::ed25519(key): put_spki(&out, OID_ED25519[..], 0u32, curve::p256, (*key)[..]);
    case variant public_key::x25519(key): put_spki(&out, OID_X25519[..], 0u32, curve::p256, (*key)[..]);
    }
    return move out;
}

/* The PEM text of the SubjectPublicKeyInfo under the label PUBLIC KEY. */
std.string::string public_key::to_pem(const public_key* this) throws std.alloc::alloc_error {
    bytes der = this->to_der();
    bytes out = std.bytes::with_capacity(len(der) * 2usize + 64usize);
    put_pem(&out, "PUBLIC KEY", der.as_slice());
    try {
        return std.string::from_utf8(out.as_slice());
    } catch (std.string::string_error rejected) {
        rejected as void;
    }
    return std.string::create();
}
