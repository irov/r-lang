module example.notary.pki;
import std.crypto;
import std.encoding;
import example.notary.keys;

/* Public-key signatures and keys in the formats of other tools: an ES256 key (ECDSA on P-256)
   whose private scalar comes from the passphrase, kept in PKCS#8 and SPKI PEM, and a fresh RSA
   key; and AES-CBC, which older protocols still require. */

protected void line(std.string::string* out, std.string::string text) throws std.alloc::alloc_error {
    std.string::append_str(out, text);
    std.string::push_scalar(out, '\n');
}

protected const str message = "Pay 10 coins to Bob";

/* Whether an ECDSA public key verifies a signature of `text`. */
protected bool checks(const std.crypto::public_key* key, str text, const u8[] signature) throws std.crypto::crypto_error {
    switch (*key) {
    case variant std.crypto::public_key::ecdsa(point): return point->verify(text, signature);
    default: return false;
    }
}

/* The signature that an ECDSA private key makes of `text`, empty for another kind of key. */
protected bytes signs(const std.crypto::private_key* key, str text) throws std.alloc::alloc_error, std.crypto::crypto_error {
    switch (*key) {
    case variant std.crypto::private_key::ecdsa(pair): return pair->sign(text);
    default:
        bytes none = {};
        return move none;
    }
}

std.string::string keys(str passphrase) throws std.alloc::alloc_error, std.crypto::crypto_error {
    std.string::string out = std.string::create();
    std.secret::buffer scalar = example.notary.keys::derived(passphrase, "es256", 32usize);
    std.crypto::ecdsa_key key = std.crypto::ecdsa_key::from_scalar(std.crypto::curve::p256, std.secret::as_slice(&scalar));
    bytes signature = key.sign(message);
    std.crypto::ecdsa_public_key point = key.public_key();
    std.crypto::curve which = point.curve();
    usize size = which.signature_length();
    std.string::string signature_hex = std.encoding::encode_hex(signature.as_slice());
    std.crypto::private_key wrapped = std.crypto::private_key::ecdsa(move key);
    std.crypto::public_key public_part = wrapped.public_key();
    std.string::string public_pem = public_part.to_pem();
    std.string::append_str(&out, public_pem);
    line(&out, f"ES256 signature, {size} bytes: {signature_hex}");
    // Both keys read back from their PEM text: the signature is deterministic (RFC 6979).
    std.secret::buffer private_pem = wrapped.to_pem();
    std.crypto::private_key read = std.crypto::private_key::from_pem(std.secret::as_slice(&private_pem));
    std.crypto::public_key read_public = std.crypto::public_key::from_pem(public_pem);
    bool valid = checks(&read_public, message, signature.as_slice());
    bool other = checks(&read_public, "Pay 11 coins to Bob", signature.as_slice());
    bytes again = signs(&read, message);
    bool same = std.bytes::equal(again.as_slice(), signature.as_slice());
    line(&out, f"read back from PEM: valid {valid}, other message {other}, same signature {same}");
    // A fresh RSA key signs with PKCS#1 v1.5 and with PSS.
    std.crypto::rsa_key rsa = std.crypto::rsa_key::generate(2048usize);
    bytes plain = rsa.sign(std.crypto::rsa_scheme::pkcs1_sha256, message);
    bytes salted = rsa.sign(std.crypto::rsa_scheme::pss_sha256, message);
    std.crypto::rsa_public_key rsa_public = rsa.public_key();
    usize bits = rsa_public.bits();
    bool plain_valid = rsa_public.verify(std.crypto::rsa_scheme::pkcs1_sha256, message, plain.as_slice());
    bool salted_valid = rsa_public.verify(std.crypto::rsa_scheme::pss_sha256, message, salted.as_slice());
    bool crossed = rsa_public.verify(std.crypto::rsa_scheme::pss_sha256, message, plain.as_slice());
    line(&out, f"RSA {bits} bits: PKCS#1 v1.5 {plain_valid}, PSS {salted_valid}, PSS check of the PKCS#1 signature {crossed}");
    return move out;
}

/* AES-128-CBC of `text` under a key and an IV of the passphrase, read back, and the refusal of
   a changed last block. */
std.string::string cbc(str passphrase, str text) throws std.alloc::alloc_error, std.crypto::crypto_error {
    std.string::string out = std.string::create();
    std.secret::buffer key = example.notary.keys::derived(passphrase, "cbc-key", 16usize);
    std.secret::buffer iv = example.notary.keys::derived(passphrase, "cbc-iv", 16usize);
    bytes sealed = std.crypto::cbc_encrypt(std.secret::as_slice(&key), std.secret::as_slice(&iv), text);
    std.string::string sealed_hex = std.encoding::encode_hex(sealed.as_slice());
    line(&out, f"ciphertext {sealed_hex}");
    bytes opened = std.crypto::cbc_decrypt(std.secret::as_slice(&key), std.secret::as_slice(&iv), sealed.as_slice());
    std.string::string opened_hex = std.encoding::encode_hex(opened.as_slice());
    line(&out, f"plaintext {opened_hex}");
    usize last = len(sealed) - 1usize;
    sealed[last] = (sealed[last] ^ 1u8) as u8;
    try {
        bytes changed = std.crypto::cbc_decrypt(std.secret::as_slice(&key), std.secret::as_slice(&iv), sealed.as_slice());
        usize changed_length = len(changed);
        line(&out, f"changed last block: {changed_length} bytes with a padding that happens to check");
    } catch (std.crypto::crypto_error failure) {
        std.crypto::error_code code = failure.code;
        line(&out, f"changed last block: {code}");
    }
    return move out;
}
