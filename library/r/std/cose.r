module std.cose;
import std.cbor;
import std.crypto;
import std.hash;

/* R-SLIB-COSE-0001: why a COSE message of RFC 9052 could not be made, read or verified. */
@derive(format)
enum error_code {
    malformed,
    wrong_type,
    unsupported_algorithm,
    unsupported_header,
    missing_header,
    invalid_key,
    verification_failed,
};

error cose_error { error_code code; };

protected cose_error failure(error_code code) { return cose_error {.code = code}; }

/* Header labels of RFC 9052 section 3.1 and algorithm identifiers of RFC 9053. */
const i64 ALGORITHM = 1i64;
const i64 CRITICAL = 2i64;
const i64 CONTENT_TYPE = 3i64;
const i64 KEY_ID = 4i64;
const i64 IV = 5i64;
const i64 PARTIAL_IV = 6i64;

const i64 EDDSA = -8i64;
const i64 HMAC_256_64 = 4i64;
const i64 HMAC_256 = 5i64;
const i64 HMAC_384 = 6i64;
const i64 HMAC_512 = 7i64;
const i64 CHACHA20_POLY1305 = 24i64;

/* The CBOR tags of the three message types. */
const u64 ENCRYPT0 = 16u64;
const u64 MAC0 = 17u64;
const u64 SIGN1 = 18u64;

/* R-SLIB-COSE-0002: the header parameters of a message, in a protected bucket, which the
   signature, tag or ciphertext covers, and an unprotected bucket. */
struct headers {
    protected array<std.cbor::entry> protected_entries;
    protected array<std.cbor::entry> unprotected_entries;
};

headers headers::create() {
    return headers {.protected_entries = [], .unprotected_entries = []};
}

protected void push_entry(array<std.cbor::entry>* entries, i64 label, std.cbor::value item)
    throws std.alloc::alloc_error {
    try {
        entries->push(std.cbor::entry {.key = std.cbor::value::integer(label), .item = move item});
    } catch (std.array::push_error<std.cbor::entry> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* Adds a parameter to the protected bucket. */
void headers::protect(headers* this, i64 label, std.cbor::value item) throws std.alloc::alloc_error {
    push_entry(&this->protected_entries, label, move item);
}

/* Adds a parameter to the unprotected bucket. */
void headers::expose(headers* this, i64 label, std.cbor::value item) throws std.alloc::alloc_error {
    push_entry(&this->unprotected_entries, label, move item);
}

protected bool is_label(const std.cbor::value* key, i64 label) {
    switch (*key) {
    case variant std.cbor::value::unsigned(number): return label >= 0i64 && *number == (label as u64);
    case variant std.cbor::value::negative(number): return label < 0i64 && *number == (((-1i64) - label) as u64);
    default: return false;
    }
}

protected o<const std.cbor::value*> find_in(const array<std.cbor::entry>* entries, i64 label) {
    for (usize index = 0usize; index < len(*entries); index += 1usize) {
        if (is_label(&(*entries)[index].key, label) == true) { return o::some(&(*entries)[index].item); }
    }
    return o::none;
}

/* The value of a parameter, from the protected bucket first. */
o<const std.cbor::value*> headers::find(const headers* this, i64 label) {
    o<const std.cbor::value*> found = find_in(&this->protected_entries, label);
    switch (found) {
    case variant o::some(item): return o::some(*item);
    case variant o::none: break;
    }
    return find_in(&this->unprotected_entries, label);
}

protected o<i64> integer_of(const std.cbor::value* item) {
    switch (*item) {
    case variant std.cbor::value::unsigned(number):
        if (*number <= 0x7fffffffffffffffu64) { return o::some(*number as i64); }
        return o::none;
    case variant std.cbor::value::negative(number):
        if (*number <= 0x7fffffffffffffffu64) { return o::some((-1i64) - (*number as i64)); }
        return o::none;
    default: return o::none;
    }
}

/* The algorithm parameter, an integer identifier. */
o<i64> headers::algorithm(const headers* this) {
    o<const std.cbor::value*> found = this->find(ALGORITHM);
    switch (found) {
    case variant o::some(item): return integer_of(*item);
    case variant o::none: return o::none;
    }
}

/* The encoded protected bucket: a zero-length byte string when it is empty (RFC 9052 section
   3). */
protected bytes protected_bytes(const headers* this) throws std.alloc::alloc_error {
    if (len(this->protected_entries) == 0usize) {
        bytes empty = {};
        return move empty;
    }
    std.cbor::encoder writer = std.cbor::encoder::create();
    writer.begin_map(len(this->protected_entries) as u64);
    const std.cbor::entry[] listed = std.array::as_slice(&this->protected_entries);
    try {
        for (usize index = 0usize; index < len(listed); index += 1usize) {
            writer.value(&listed[index].key);
            writer.value(&listed[index].item);
        }
    } catch (std.cbor::cbor_error rejected) {
        rejected as void;
    }
    return writer.finish();
}

protected void write_unprotected(std.cbor::encoder* writer, const headers* fields)
    throws std.alloc::alloc_error {
    writer->begin_map(len(fields->unprotected_entries) as u64);
    const std.cbor::entry[] listed = std.array::as_slice(&fields->unprotected_entries);
    try {
        for (usize index = 0usize; index < len(listed); index += 1usize) {
            writer->value(&listed[index].key);
            writer->value(&listed[index].item);
        }
    } catch (std.cbor::cbor_error rejected) {
        rejected as void;
    }
}

/* The structure a signature or tag covers (RFC 9052 sections 4.4, 5.3, 6.3): its context
   string, the protected bucket, the external data and, but for encryption, the payload. */
protected bytes to_be_covered(str context, const u8[] protected_part, const u8[] external_aad,
                              const u8[] payload, bool with_payload) throws std.alloc::alloc_error {
    std.cbor::encoder writer = std.cbor::encoder::create();
    if (with_payload == true) {
        writer.begin_array(4u64);
    } else {
        writer.begin_array(3u64);
    }
    writer.text(context);
    writer.bytes(protected_part);
    writer.bytes(external_aad);
    if (with_payload == true) { writer.bytes(payload); }
    return writer.finish();
}

/* The message a builder writes: the tag, the protected bytes, the unprotected bucket, the
   payload or ciphertext and a final signature or tag. */
protected bytes write_message(u64 tag, const u8[] protected_part, const headers* fields, const u8[] content,
                              const u8[] last, bool with_last) throws std.alloc::alloc_error {
    std.cbor::encoder writer = std.cbor::encoder::create();
    writer.tag(tag);
    if (with_last == true) {
        writer.begin_array(4u64);
    } else {
        writer.begin_array(3u64);
    }
    writer.bytes(protected_part);
    write_unprotected(&writer, fields);
    writer.bytes(content);
    if (with_last == true) { writer.bytes(last); }
    return writer.finish();
}

/* ---- Reading ---- */

/* R-SLIB-COSE-0003: a received message, its protected bucket as received and decoded. A
   detached payload is none. */
struct message {
    u64 tag;
    bytes protected_part;
    headers fields;
    o<bytes> content;
    bytes last;
};

protected headers decode_headers(const u8[] protected_part, std.cbor::value unprotected)
    throws std.alloc::alloc_error, cose_error {
    headers fields = headers::create();
    if (len(protected_part) > 0usize) {
        try {
            std.cbor::value decoded = std.cbor::decode(protected_part);
            switch (move decoded) {
            case variant std.cbor::value::map(move entries): fields.protected_entries = move entries;
            default: throw failure(error_code::malformed);
            }
        } catch (std.cbor::cbor_error rejected) {
            rejected as void;
            throw failure(error_code::malformed);
        }
    }
    switch (move unprotected) {
    case variant std.cbor::value::map(move entries): fields.unprotected_entries = move entries;
    default: throw failure(error_code::malformed);
    }
    return move fields;
}

protected bytes byte_string(std.cbor::value item) throws cose_error {
    switch (move item) {
    case variant std.cbor::value::bytes(move data): return move data;
    default: throw failure(error_code::malformed);
    }
}

protected std.cbor::value take(array<std.cbor::value>* parts) {
    o<std.cbor::value> popped = std.array::pop(parts);
    switch (move popped) {
    case variant o::some(move item): return move item;
    case variant o::none: break;
    }
    return std.cbor::value::null_value;
}

protected message decode_parts(std.cbor::value item, u64 tag, u64 count) throws std.alloc::alloc_error, cose_error {
    switch (move item) {
    case variant std.cbor::value::sequence(move parts):
        if ((len(parts) as u64) != count) { throw failure(error_code::malformed); }
        bytes last = {};
        if (count == 4u64) { last = byte_string(take(&parts)); }
        std.cbor::value content = take(&parts);
        std.cbor::value unprotected = take(&parts);
        bytes received_part = byte_string(take(&parts));
        headers fields = decode_headers(received_part.as_slice(), move unprotected);
        // An encoded empty map stands for the empty bucket, which the structures cover as a
        // zero-length byte string (RFC 9052 section 3).
        bytes protected_part = {};
        if (len(fields.protected_entries) > 0usize) {
            protected_part = move received_part;
        } else {
            drop received_part;
        }
        o<bytes> payload = o::none;
        switch (move content) {
        case variant std.cbor::value::bytes(move data): payload = o::some(move data);
        case variant std.cbor::value::null_value: break;
        default: throw failure(error_code::malformed);
        }
        return message {.tag = tag, .protected_part = move protected_part, .fields = move fields,
                        .content = move payload, .last = move last};
    default: throw failure(error_code::malformed);
    }
}

protected std.cbor::value parse(const u8[] data) throws std.alloc::alloc_error, cose_error {
    try {
        return std.cbor::decode(data);
    } catch (std.cbor::cbor_error rejected) {
        rejected as void;
    }
    throw failure(error_code::malformed);
}

/* A message of the type `expected_tag` (16, 17 or 18), tagged with it or untagged; a message
   with another tag is wrong_type. */
message decode(const u8[] data, u64 expected_tag) throws std.alloc::alloc_error, cose_error {
    std.cbor::value item = parse(data);
    u64 count = 4u64;
    if (expected_tag == ENCRYPT0) { count = 3u64; }
    bool tagged_form = false;
    switch (item) {
    case variant std.cbor::value::tagged(found):
        if (found->tag != expected_tag) { throw failure(error_code::wrong_type); }
        tagged_form = true;
    default: break;
    }
    if (tagged_form == false) { return decode_parts(move item, expected_tag, count); }
    switch (move item) {
    case variant std.cbor::value::tagged(move tagged):
        std.cbor::value content = core::replace(&*tagged.content, std.cbor::value::null_value);
        return decode_parts(move content, expected_tag, count);
    default: throw failure(error_code::malformed);
    }
}



/* Critical parameters (label 2) shall name only the parameters this module understands. */
protected void check_critical(const headers* fields) throws cose_error {
    o<const std.cbor::value*> found = find_in(&fields->protected_entries, CRITICAL);
    switch (found) {
    case variant o::some(item):
        switch (**item) {
        case variant std.cbor::value::sequence(labels):
            const std.cbor::value[] listed = std.array::as_slice(labels);
            if (len(listed) == 0usize) { throw failure(error_code::malformed); }
            for (usize index = 0usize; index < len(listed); index += 1usize) {
                o<i64> label = integer_of(&listed[index]);
                switch (label) {
                case variant o::some(number):
                    if (*number < 1i64 || *number > 6i64) { throw failure(error_code::unsupported_header); }
                case variant o::none: throw failure(error_code::unsupported_header);
                }
            }
        default: throw failure(error_code::malformed);
        }
    case variant o::none: break;
    }
}

protected i64 algorithm_of(const headers* fields) throws cose_error {
    check_critical(fields);
    o<i64> found = fields->algorithm();
    switch (found) {
    case variant o::some(number): return *number;
    case variant o::none: break;
    }
    throw failure(error_code::missing_header);
}

protected const u8[] payload_of(const message* received) throws cose_error {
    switch (received->content) {
    case variant o::some(data): return std.array::as_slice(data);
    case variant o::none: break;
    }
    throw failure(error_code::missing_header);
}

/* ---- COSE_Sign1 ---- */

/* R-SLIB-COSE-0004: a COSE_Sign1 message (tag 18) with an EdDSA signature over `payload`; when
   the headers name no algorithm, EdDSA is added to the protected bucket. */
bytes sign1(headers fields, const u8[] payload, const std.crypto::signing_key* key, const u8[] external_aad)
    throws std.alloc::alloc_error, std.crypto::crypto_error, cose_error {
    headers kept = move fields;
    o<i64> named = kept.algorithm();
    switch (named) {
    case variant o::some(number): if (*number != EDDSA) { throw failure(error_code::unsupported_algorithm); }
    case variant o::none: kept.protect(ALGORITHM, std.cbor::value::integer(EDDSA));
    }
    bytes protected_part = protected_bytes(&kept);
    bytes covered = to_be_covered("Signature1", protected_part.as_slice(), external_aad, payload, true);
    u8[64] signature = key->sign(covered.as_slice());
    return write_message(SIGN1, protected_part.as_slice(), &kept, payload, signature[..], true);
}

/* The message after its EdDSA signature by `public_key` verifies. */
message verify_sign1(const u8[] data, const u8[] public_key, const u8[] external_aad)
    throws std.alloc::alloc_error, std.crypto::crypto_error, cose_error {
    message received = decode(data, SIGN1);
    if (algorithm_of(&received.fields) != EDDSA) { throw failure(error_code::unsupported_algorithm); }
    if (len(public_key) != 32usize) { throw failure(error_code::invalid_key); }
    bytes covered = to_be_covered("Signature1", received.protected_part.as_slice(), external_aad,
                                  payload_of(&received), true);
    if (std.crypto::verify(public_key, covered.as_slice(), received.last.as_slice()) == false) {
        throw failure(error_code::verification_failed);
    }
    return move received;
}

/* ---- COSE_Mac0 ---- */

protected bytes hmac(i64 algorithm, const u8[] key, const u8[] data) throws std.alloc::alloc_error, cose_error {
    bytes tag = {};
    if (algorithm == HMAC_256 || algorithm == HMAC_256_64) {
        std.hash::sha256_digest digest = std.hash::hmac_sha256(key, data);
        std.bytes::append(&tag, digest.bytes[..]);
        if (algorithm == HMAC_256_64) {
            const u8[] full = tag.as_slice();
            bytes truncated = {};
            std.bytes::append(&truncated, full[0usize..8usize]);
            return move truncated;
        }
        return move tag;
    }
    if (algorithm == HMAC_384) {
        u8[48] digest = std.hash::hmac_sha384(key, data);
        std.bytes::append(&tag, digest[..]);
        return move tag;
    }
    if (algorithm == HMAC_512) {
        std.hash::sha512_digest digest = std.hash::hmac_sha512(key, data);
        std.bytes::append(&tag, digest.bytes[..]);
        return move tag;
    }
    throw failure(error_code::unsupported_algorithm);
}

/* R-SLIB-COSE-0005: a COSE_Mac0 message (tag 17) with the HMAC of the algorithm the headers
   name (HMAC 256/64, 256/256, 384/384 or 512/512 of RFC 9053). */
bytes mac0(headers fields, const u8[] payload, const u8[] key, const u8[] external_aad)
    throws std.alloc::alloc_error, cose_error {
    const i64 algorithm = algorithm_of(&fields);
    bytes protected_part = protected_bytes(&fields);
    bytes covered = to_be_covered("MAC0", protected_part.as_slice(), external_aad, payload, true);
    bytes tag = hmac(algorithm, key, covered.as_slice());
    return write_message(MAC0, protected_part.as_slice(), &fields, payload, tag.as_slice(), true);
}

/* The message after its tag under `key` verifies, compared in constant time. */
message verify_mac0(const u8[] data, const u8[] key, const u8[] external_aad) throws std.alloc::alloc_error, cose_error {
    message received = decode(data, MAC0);
    const i64 algorithm = algorithm_of(&received.fields);
    bytes covered = to_be_covered("MAC0", received.protected_part.as_slice(), external_aad,
                                  payload_of(&received), true);
    bytes expected = hmac(algorithm, key, covered.as_slice());
    if (std.secret::constant_time_equal(expected.as_slice(), received.last.as_slice()) == false) {
        throw failure(error_code::verification_failed);
    }
    return move received;
}

/* ---- COSE_Encrypt0 ---- */

/* R-SLIB-COSE-0006: a COSE_Encrypt0 message (tag 16) encrypted with ChaCha20/Poly1305 under a
   32-byte key; the 12-byte `iv` is added to the unprotected bucket. When the headers name no
   algorithm, ChaCha20/Poly1305 is added to the protected bucket. */
bytes encrypt0(headers fields, const u8[] plaintext, const u8[] key, const u8[] iv, const u8[] external_aad)
    throws std.alloc::alloc_error, std.crypto::crypto_error, cose_error {
    headers kept = move fields;
    o<i64> named = kept.algorithm();
    switch (named) {
    case variant o::some(number):
        if (*number != CHACHA20_POLY1305) { throw failure(error_code::unsupported_algorithm); }
    case variant o::none: kept.protect(ALGORITHM, std.cbor::value::integer(CHACHA20_POLY1305));
    }
    if (len(key) != 32usize || len(iv) != 12usize) { throw failure(error_code::invalid_key); }
    kept.expose(IV, std.cbor::value::of_bytes(iv));
    bytes protected_part = protected_bytes(&kept);
    bytes covered = to_be_covered("Encrypt0", protected_part.as_slice(), external_aad, external_aad, false);
    bytes sealed = std.crypto::seal(std.crypto::aead::chacha20_poly1305, key, iv, covered.as_slice(), plaintext);
    return write_message(ENCRYPT0, protected_part.as_slice(), &kept, sealed.as_slice(), sealed.as_slice(), false);
}

protected bytes open_content(const message* received, const u8[] key, const u8[] iv, const u8[] external_aad)
    throws std.alloc::alloc_error, cose_error {
    if (len(key) != 32usize || len(iv) != 12usize) { throw failure(error_code::invalid_key); }
    bytes covered = to_be_covered("Encrypt0", received->protected_part.as_slice(), external_aad, external_aad, false);
    try {
        return std.crypto::open(std.crypto::aead::chacha20_poly1305, key, iv, covered.as_slice(),
                                payload_of(received));
    } catch (std.crypto::crypto_error rejected) {
        rejected as void;
    }
    throw failure(error_code::verification_failed);
}

/* The plaintext of a COSE_Encrypt0 message under `key`; a message that does not authenticate
   is verification_failed. */
bytes decrypt0(const u8[] data, const u8[] key, const u8[] external_aad)
    throws std.alloc::alloc_error, cose_error {
    message received = decode(data, ENCRYPT0);
    if (algorithm_of(&received.fields) != CHACHA20_POLY1305) { throw failure(error_code::unsupported_algorithm); }
    o<const std.cbor::value*> found = received.fields.find(IV);
    switch (found) {
    case variant o::some(item):
        switch (**item) {
        case variant std.cbor::value::bytes(data_of):
            return open_content(&received, key, std.array::as_slice(data_of), external_aad);
        default: throw failure(error_code::malformed);
        }
    case variant o::none: throw failure(error_code::missing_header);
    }
    throw failure(error_code::malformed);
}

