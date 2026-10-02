module example.notary.keys;
import std.crypto;

/* The keys of a passphrase: Argon2id stretches it with the salt of this example into 32 bytes,
   which are the seed of the signing key; HKDF derives the sealing key from them. A real
   service would keep a random salt per user. */
protected const str salt_text = "example.notary.1";

protected std.crypto::password_limits limits() {
    return std.crypto::password_limits {.operations = 2u64, .memory = 8388608usize};
}

protected std.secret::buffer stretched(str passphrase) throws std.alloc::alloc_error, std.crypto::crypto_error {
    bytes seed = std.crypto::argon2id(passphrase, salt_text, limits(), 32usize);
    return std.secret::from_bytes(move seed);
}

std.crypto::signing_key signer(str passphrase) throws std.alloc::alloc_error, std.crypto::crypto_error {
    std.secret::buffer seed = stretched(passphrase);
    return std.crypto::signing_key::from_seed(std.secret::as_slice(&seed));
}

std.secret::buffer sealing_key(str passphrase) throws std.alloc::alloc_error, std.crypto::crypto_error {
    std.secret::buffer seed = stretched(passphrase);
    bytes key = std.crypto::hkdf_sha256("example.notary", std.secret::as_slice(&seed), "seal", 32usize);
    return std.secret::from_bytes(move key);
}
