module std.crypto;

/* R-SLIB-CRYPTO-0001: the R part of std.crypto over its native provider std.crypto.native, which
   calls libsodium. Every provider function reads and writes the buffers it is given only. */

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
