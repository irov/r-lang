module example.notary.tools;
import std.encoding;
import std.hash;
import std.cbor;
import std.crypto;
import std.cose;

/* The remaining commands of notary: a key agreement, password hashes, digests, MAC tags and a
   CBOR document written item by item. */

protected void line(std.string::string* out, std.string::string text) throws std.alloc::alloc_error {
    std.string::append_str(out, text);
    std.string::push_scalar(out, '\n');
}

/* Alice and Bob agree on a secret with X25519, derive a key with HKDF-SHA-512 and exchange a
   note sealed with XChaCha20-Poly1305. */
std.string::string exchange(str note) throws std.alloc::alloc_error, std.crypto::crypto_error {
    std.string::string out = std.string::create();
    std.crypto::exchange_key alice = std.crypto::exchange_key::generate();
    std.crypto::exchange_key bob = std.crypto::exchange_key::generate();
    std.secret::buffer alice_secret = alice.shared(bob.public_key[..]);
    std.secret::buffer bob_secret = bob.shared(alice.public_key[..]);
    bool agreed = std.secret::constant_time_equal(std.secret::as_slice(&alice_secret),
                                                  std.secret::as_slice(&bob_secret));
    line(&out, f"agreed: {agreed}");
    std.crypto::aead algorithm = std.crypto::aead::xchacha20_poly1305;
    bytes key = std.crypto::hkdf_sha512("notary exchange", std.secret::as_slice(&alice_secret), "note",
                                        algorithm.key_length());
    bytes nonce = std.crypto::random(algorithm.nonce_length());
    bytes sealed = std.crypto::seal(algorithm, key.as_slice(), nonce.as_slice(), alice.public_key[..], note);
    usize overhead = len(sealed) - len(note);
    usize tag = algorithm.tag_length();
    line(&out, f"sealed with {overhead} bytes of tag ({tag})");
    bytes bob_key = std.crypto::hkdf_sha512("notary exchange", std.secret::as_slice(&bob_secret), "note", 32usize);
    bytes opened = std.crypto::open(algorithm, bob_key.as_slice(), nonce.as_slice(), alice.public_key[..],
                                    sealed.as_slice());
    std.string::string text = std.encoding::encode_hex(opened.as_slice());
    line(&out, f"bob read {text}");
    return move out;
}

/* An Argon2id password hash with the interactive limits and its checks. */
std.string::string password(str secret) throws std.alloc::alloc_error, std.crypto::crypto_error {
    std.string::string out = std.string::create();
    std.crypto::password_limits limits = std.crypto::password_limits::interactive();
    std.string::string hash = std.crypto::password_hash(secret, limits);
    const u8[] text = hash;
    bool shaped = len(text) > 10usize && std.bytes::starts_with(text, "$argon2id$");
    line(&out, f"argon2id hash: {shaped}");
    bool same = std.crypto::password_verify(hash, secret);
    bool other = std.crypto::password_verify(hash, "not the password");
    line(&out, f"same password: {same}, other password: {other}");
    return move out;
}

/* BLAKE2b of the text at once and in two pieces, SHA-384 and HMAC-SHA-384. */
std.string::string digests(str text) throws std.alloc::alloc_error, std.crypto::crypto_error {
    std.string::string out = std.string::create();
    bytes whole = std.crypto::blake2b(text, "", 32usize);
    std.crypto::blake2b_state state = std.crypto::blake2b_state::create("", 32usize);
    const u8[] raw_text = text;
    const usize half = len(raw_text) / 2usize;
    state.update(raw_text[0usize..half]);
    state.update(raw_text[half..len(raw_text)]);
    bytes pieces = state.finish();
    std.string::string whole_hex = std.encoding::encode_hex(whole.as_slice());
    bool same = std.bytes::equal(whole.as_slice(), pieces.as_slice());
    line(&out, f"blake2b-256 {whole_hex} (pieces agree: {same})");
    std.hash::sha384_state sha = std.hash::sha384_state::create();
    sha.update(raw_text);
    u8[48] streamed = sha.finish();
    u8[48] direct = std.hash::sha384(text);
    std.string::string sha_hex = std.encoding::encode_hex(direct[..]);
    bool sha_same = std.bytes::equal(streamed[..], direct[..]);
    line(&out, f"sha-384 {sha_hex} (pieces agree: {sha_same})");
    u8[48] mac = std.hash::hmac_sha384("notary", text);
    std.string::string mac_hex = std.encoding::encode_hex(mac[..]);
    line(&out, f"hmac-sha-384 {mac_hex}");
    return move out;
}

/* A COSE_Mac0 message of `message` under the key, tagged with HMAC 256/64, and its check. */
std.string::string tagged(str key, str message) throws std.alloc::alloc_error, std.cose::cose_error {
    std.string::string out = std.string::create();
    i64[4] algorithms = {std.cose::HMAC_256_64, std.cose::HMAC_256, std.cose::HMAC_384, std.cose::HMAC_512};
    for (usize index = 0usize; index < 4usize; index += 1usize) {
        std.cose::headers fields = std.cose::headers::create();
        fields.protect(std.cose::ALGORITHM, std.cbor::value::integer(algorithms[index]));
        fields.protect(std.cose::CONTENT_TYPE, std.cbor::value::of_text("text/plain"));
        bytes sealed = std.cose::mac0(move fields, message, key, "");
        std.cose::message checked = std.cose::verify_mac0(sealed.as_slice(), key, "");
        usize tag = len(checked.last);
        i64 algorithm = algorithms[index];
        line(&out, f"mac0 algorithm {algorithm}: {tag}-byte tag");
    }
    return move out;
}

/* A CBOR document written item by item, read back in deterministic form. */
std.string::string document() throws std.alloc::alloc_error, std.cbor::cbor_error {
    std.cbor::encoder writer = std.cbor::encoder::create();
    // The keys go in the bytewise order of their encodings, so the document is deterministic.
    writer.begin_map(3u64);
    writer.text("note");
    std.cbor::value nested = std.cbor::value::tag_of(24u64, std.cbor::value::of_bytes("hi"));
    writer.value(&nested);
    writer.text("flags");
    writer.begin_array(6u64);
    writer.boolean(true);
    writer.null_value();
    writer.undefined();
    writer.simple(32u8);
    writer.float(0.5);
    writer.negative(9u64);
    writer.text("issued");
    writer.tag(1u64);
    writer.unsigned(1700000000u64);
    usize written = writer.length();
    bytes data = writer.finish();
    std.cbor::value item = std.cbor::decode_deterministic(data.as_slice());
    std.string::string text = std.cbor::diagnostic(&item);
    bytes again = std.cbor::encode(&item);
    bool same = std.bytes::equal(again.as_slice(), data.as_slice());
    // A map value built from entries in any order encodes with its keys in order as well.
    array<std.cbor::entry> entries = [];
    try {
        entries.push(std.cbor::entry {.key = std.cbor::value::of_text("issued"), .item = std.cbor::value::integer(1i64)});
        entries.push(std.cbor::entry {.key = std.cbor::value::of_text("note"), .item = std.cbor::value::integer(2i64)});
    } catch (std.array::push_error<std.cbor::entry> failure) {
        drop failure;
        throw std.alloc::alloc_error::out_of_memory;
    }
    std.cbor::value built = std.cbor::value::map(move entries);
    bytes built_bytes = std.cbor::encode(&built);
    std.string::string built_text = std.cbor::diagnostic(&built);
    std.string::string built_hex = std.encoding::encode_hex(built_bytes.as_slice());
    std.cbor::encoder copy = std.cbor::encoder::create();
    copy.encoded(data.as_slice());
    copy.integer(-1i64);
    bytes copied = copy.finish();
    usize copied_length = len(copied);
    return f"{written} bytes {text}\nencode agrees: {same}, {copied_length} bytes with a trailing -1\n"
           f"entries {built_text} encode as {built_hex}\n";
}
