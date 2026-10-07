module std.jwt;
import std.crypto;
import std.encoding;
import std.hash;

/* R-SLIB-JWT-0001: JSON Web Signatures in the compact serialization (RFC 7515), JSON Web Tokens
   (RFC 7519) and JSON Web Keys (RFC 7517, RFC 7518) over std.crypto, std.hash and std.json. */

/* R-SLIB-JWT-0001: why a token was not made or not accepted. */
@derive(format)
enum error_code {
    malformed,
    unsupported_algorithm,
    key_mismatch,
    no_key,
    invalid_signature,
    expired,
    not_yet_valid,
    invalid_issuer,
    invalid_audience,
    missing_claim,
    invalid_claim,
    invalid_key,
};

error jwt_error { error_code code; };

protected jwt_error failure(error_code code) { return jwt_error {.code = code}; }

/* R-SLIB-JWT-0002: the signature algorithms of RFC 7518 section 3 and RFC 8037. */
enum algorithm { hs256, hs384, hs512, rs256, rs384, rs512, ps256, ps384, ps512, es256, es384, eddsa };

/* The name of the algorithm in the `alg` header parameter. */
str algorithm::name(algorithm this) {
    switch (this) {
    case algorithm::hs256: return "HS256";
    case algorithm::hs384: return "HS384";
    case algorithm::hs512: return "HS512";
    case algorithm::rs256: return "RS256";
    case algorithm::rs384: return "RS384";
    case algorithm::rs512: return "RS512";
    case algorithm::ps256: return "PS256";
    case algorithm::ps384: return "PS384";
    case algorithm::ps512: return "PS512";
    case algorithm::es256: return "ES256";
    case algorithm::es384: return "ES384";
    case algorithm::eddsa: return "EdDSA";
    }
}

/* The algorithm of an `alg` name; `none` and every other name are unsupported_algorithm. */
algorithm algorithm::parse(str name) throws jwt_error {
    switch (name) {
    case "HS256": return algorithm::hs256;
    case "HS384": return algorithm::hs384;
    case "HS512": return algorithm::hs512;
    case "RS256": return algorithm::rs256;
    case "RS384": return algorithm::rs384;
    case "RS512": return algorithm::rs512;
    case "PS256": return algorithm::ps256;
    case "PS384": return algorithm::ps384;
    case "PS512": return algorithm::ps512;
    case "ES256": return algorithm::es256;
    case "ES384": return algorithm::es384;
    case "EdDSA": return algorithm::eddsa;
    default: throw failure(error_code::unsupported_algorithm);
    }
}

/* The algorithm of the bytes of an `alg` member; an absent one is malformed. */
protected algorithm algorithm_named(const u8[] name) throws jwt_error {
    throw (len(name) == 0usize) failure(error_code::malformed);
    try {
        return algorithm::parse(core::validate_utf8(name));
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    throw failure(error_code::malformed);
}

protected bool is_hmac(algorithm alg) {
    return alg == algorithm::hs256 || alg == algorithm::hs384 || alg == algorithm::hs512;
}

protected bool is_rsa(algorithm alg) {
    return alg == algorithm::rs256 || alg == algorithm::rs384 || alg == algorithm::rs512 ||
           alg == algorithm::ps256 || alg == algorithm::ps384 || alg == algorithm::ps512;
}

/* The bytes of the hash of an HMAC algorithm, the least length of its key (RFC 7518 3.2). */
protected usize hmac_length(algorithm alg) {
    if (alg == algorithm::hs384) { return 48usize; }
    if (alg == algorithm::hs512) { return 64usize; }
    return 32usize;
}

protected std.crypto::rsa_scheme scheme_of(algorithm alg) {
    switch (alg) {
    case algorithm::rs384: return std.crypto::rsa_scheme::pkcs1_sha384;
    case algorithm::rs512: return std.crypto::rsa_scheme::pkcs1_sha512;
    case algorithm::ps256: return std.crypto::rsa_scheme::pss_sha256;
    case algorithm::ps384: return std.crypto::rsa_scheme::pss_sha384;
    case algorithm::ps512: return std.crypto::rsa_scheme::pss_sha512;
    default: return std.crypto::rsa_scheme::pkcs1_sha256;
    }
}

/* The HMAC of an HS algorithm. */
protected bytes hmac_of(algorithm alg, const u8[] key, const u8[] message) throws std.alloc::alloc_error {
    bytes out = std.bytes::with_capacity(64usize);
    if (alg == algorithm::hs384) {
        u8[48] digest = std.hash::hmac_sha384(key, message);
        std.bytes::append(&out, digest[..]);
        return move out;
    }
    if (alg == algorithm::hs512) {
        std.hash::sha512_digest digest = std.hash::hmac_sha512(key, message);
        std.bytes::append(&out, &digest.bytes);
        return move out;
    }
    std.hash::sha256_digest digest = std.hash::hmac_sha256(key, message);
    std.bytes::append(&out, &digest.bytes);
    return move out;
}

/* The key material of a signer or a verifier. */
protected enum material {
    hmac(std.secret::buffer),
    private_key(std.crypto::private_key),
    public_key(std.crypto::public_key),
};

/* Whether a private key signs with the algorithm. */
protected bool private_fits(algorithm alg, const std.crypto::private_key* key) {
    switch (*key) {
    case variant std.crypto::private_key::ecdsa(pair):
        if (alg == algorithm::es256) { return pair->curve() == std.crypto::curve::p256; }
        if (alg == algorithm::es384) { return pair->curve() == std.crypto::curve::p384; }
        return false;
    case variant std.crypto::private_key::rsa(_): return is_rsa(alg);
    case variant std.crypto::private_key::ed25519(_): return alg == algorithm::eddsa;
    case variant std.crypto::private_key::x25519(_): return false;
    }
}

/* Whether a public key verifies the algorithm. */
protected bool public_fits(algorithm alg, const std.crypto::public_key* key) {
    switch (*key) {
    case variant std.crypto::public_key::ecdsa(point):
        if (alg == algorithm::es256) { return point->curve() == std.crypto::curve::p256; }
        if (alg == algorithm::es384) { return point->curve() == std.crypto::curve::p384; }
        return false;
    case variant std.crypto::public_key::rsa(_): return is_rsa(alg);
    case variant std.crypto::public_key::ed25519(_): return alg == algorithm::eddsa;
    case variant std.crypto::public_key::x25519(_): return false;
    }
}

/* Whether key material verifies the algorithm. */
protected bool material_fits(algorithm alg, const material* key) {
    switch (*key) {
    case variant material::hmac(secret): return is_hmac(alg) && std.secret::len(secret) >= hmac_length(alg);
    case variant material::private_key(pair): return private_fits(alg, pair);
    case variant material::public_key(point): return public_fits(alg, point);
    }
}

protected std.secret::buffer secret_of(const u8[] secret) throws std.alloc::alloc_error {
    bytes copied = std.bytes::with_capacity(len(secret));
    std.bytes::append(&copied, secret);
    return std.secret::from_bytes(move copied);
}

/* R-SLIB-JWT-0003: a key that signs tokens with one algorithm, and the key identifier its
   tokens name. */
struct signer {
    protected algorithm alg;
    protected std.string::string kid;
    protected material key;
};

/* A signer of an HS algorithm with a secret of at least the hash length. */
signer signer::hmac(algorithm alg, const u8[] secret) throws jwt_error, std.alloc::alloc_error {
    throw (is_hmac(alg) == false || len(secret) < hmac_length(alg)) failure(error_code::key_mismatch);
    return signer {.alg = alg, .kid = std.string::create(), .key = material::hmac(secret_of(secret))};
}

/* A signer of an RS, PS, ES or EdDSA algorithm with a private key of its kind. */
signer signer::with_private_key(algorithm alg, std.crypto::private_key key) throws jwt_error {
    throw (private_fits(alg, &key) == false) failure(error_code::key_mismatch);
    return signer {.alg = alg, .kid = std.string::create(), .key = material::private_key(move key)};
}

/* Names the key in the `kid` header parameter of the tokens it signs; empty for none. */
void signer::set_key_id(signer* this, str kid) throws std.alloc::alloc_error {
    std.string::string old = core::replace(&this->kid, std.string::from_str(kid));
    drop old;
}

/* R-SLIB-JWT-0004: a key that verifies tokens, with the algorithm it is limited to, if any, and
   its key identifier, empty for none. */
struct verifier {
    protected o<algorithm> alg;
    protected std.string::string kid;
    protected material key;
};

verifier verifier::hmac(algorithm alg, const u8[] secret) throws jwt_error, std.alloc::alloc_error {
    throw (is_hmac(alg) == false || len(secret) < hmac_length(alg)) failure(error_code::key_mismatch);
    return verifier {.alg = o::some(alg), .kid = std.string::create(), .key = material::hmac(secret_of(secret))};
}

verifier verifier::with_public_key(algorithm alg, std.crypto::public_key key) throws jwt_error {
    throw (public_fits(alg, &key) == false) failure(error_code::key_mismatch);
    return verifier {.alg = o::some(alg), .kid = std.string::create(), .key = material::public_key(move key)};
}

void verifier::set_key_id(verifier* this, str kid) throws std.alloc::alloc_error {
    std.string::string old = core::replace(&this->kid, std.string::from_str(kid));
    drop old;
}

/* Whether this key may verify a token of the algorithm and key identifier. */
protected bool verifier::accepts(const verifier* this, algorithm alg, const u8[] kid) {
    switch (this->alg) {
    case variant o::some(limited):
        if (*limited != alg) { return false; }
    case variant o::none: break;
    }
    if (len(kid) > 0usize && std.bytes::equal(this->kid, kid) == false) { return false; }
    return material_fits(alg, &this->key);
}

/* R-SLIB-JWT-0004: the keys a verification chooses from. */
struct key_set { protected array<verifier> keys; };

key_set key_set::create() {
    return key_set {.keys = std.array::create::<verifier>()};
}

void key_set::add(key_set* this, verifier key) throws std.alloc::alloc_error {
    try {
        this->keys.push(move key);
    } catch (std.array::push_error<verifier> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

usize key_set::size(const key_set* this) {
    return len(this->keys);
}

/* ---- Compact serialization ---- */

protected std.string::string base64url(const u8[] data) throws std.alloc::alloc_error {
    return std.encoding::encode_base64_url(data);
}

protected bytes unbase64url(const u8[] text) throws jwt_error, std.alloc::alloc_error {
    try {
        str checked = core::validate_utf8(text);
        return std.encoding::decode_base64_url(checked);
    } catch (core::utf8_error rejected) {
        rejected as void;
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    }
    throw failure(error_code::malformed);
}

protected std.json::value parse_object(const u8[] text) throws jwt_error, std.alloc::alloc_error {
    try {
        std.json::value parsed = std.json::parse(text);
        throw (std.json::kind(&parsed) != std.json::value_kind::object) failure(error_code::malformed);
        return move parsed;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::malformed);
}

/* The signature of the signing input with a signer. */
protected bytes signature_of(const signer* key, const u8[] input) throws jwt_error, std.alloc::alloc_error {
    try {
        switch (key->key) {
        case variant material::hmac(secret): return hmac_of(key->alg, std.secret::as_slice(secret), input);
        case variant material::private_key(pair):
            switch (*pair) {
            case variant std.crypto::private_key::ecdsa(ecdsa): return ecdsa->sign(input);
            case variant std.crypto::private_key::rsa(rsa): return rsa->sign(scheme_of(key->alg), input);
            case variant std.crypto::private_key::ed25519(edwards):
                u8[64] signature = edwards->sign(input);
                bytes out = std.bytes::with_capacity(64usize);
                std.bytes::append(&out, signature[..]);
                return move out;
            case variant std.crypto::private_key::x25519(_): throw failure(error_code::key_mismatch);
            }
        case variant material::public_key(_): throw failure(error_code::key_mismatch);
        }
    } catch (std.crypto::crypto_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_key);
}

/* The protected header of a token of a signer: `alg`, `typ` JWT and `kid` when it has one. */
protected std.string::string header_of(const signer* key) throws jwt_error, std.alloc::alloc_error {
    try {
        std.json::value head = std.json::object();
        str alg_name = key->alg.name();
        const u8[] alg_bytes = alg_name;
        std.json::insert(&head, "alg", std.json::from_string(alg_bytes));
        std.json::insert(&head, "typ", std.json::from_string("JWT"));
        const u8[] kid = key->kid;
        if (len(kid) > 0usize) { std.json::insert(&head, "kid", std.json::from_string(kid)); }
        return std.json::stringify(&head);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::malformed);
}

/* The compact JWS of a payload: header, payload and signature in base64url, joined by dots. */
protected std.string::string compact(const signer* key, const u8[] payload) throws jwt_error, std.alloc::alloc_error {
    std.string::string head = header_of(key);
    std.string::string input = base64url(head);
    input.append(".");
    std.string::string encoded = base64url(payload);
    input.append(encoded);
    bytes signature = signature_of(key, input);
    input.append(".");
    std.string::string signature_text = base64url(signature.as_slice());
    input.append(signature_text);
    return move input;
}

/* R-SLIB-JWT-0005: a token with the claims of a JSON object. */
std.string::string sign(const signer* key, const std.json::value* claims) throws jwt_error, std.alloc::alloc_error {
    throw (std.json::kind(claims) != std.json::value_kind::object) failure(error_code::malformed);
    try {
        std.string::string payload = std.json::stringify(claims);
        return compact(key, payload);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::malformed);
}

/* R-SLIB-JWT-0005: a token with the claims of a value that std.json writes as an object. */
@generic<T: json_encode>
std.string::string sign_claims(const signer* key, const T* claims) throws jwt_error, std.alloc::alloc_error {
    try {
        std.string::string payload = std.json::marshal(claims);
        const u8[] text = payload;
        throw (len(text) == 0usize || text[0usize] != 123u8) failure(error_code::malformed);
        return compact(key, text);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::malformed);
}

/* The three parts of a compact token. */
protected struct parts { usize first_dot; usize second_dot; };

protected parts split(const u8[] token) throws jwt_error {
    usize first = len(token);
    usize second = len(token);
    usize dots = 0usize;
    for (usize index = 0usize; index < len(token); index += 1usize) {
        if (token[index] == 46u8) {
            if (dots == 0usize) { first = index; }
            if (dots == 1usize) { second = index; }
            dots += 1usize;
        }
    }
    throw (dots != 2usize) failure(error_code::malformed);
    return parts {.first_dot = first, .second_dot = second};
}

/* R-SLIB-JWT-0005: the protected header of a token, read without verifying the token. */
std.json::value header(str token) throws jwt_error, std.alloc::alloc_error {
    const u8[] text = token;
    parts at = split(text);
    bytes decoded = unbase64url(text[0usize..at.first_dot]);
    return parse_object(decoded.as_slice());
}

/* The string member of an object, empty when it is absent; another type is malformed. */
protected str string_member(const std.json::value* object, str name) throws jwt_error {
    const u8[] key = name;
    switch (std.json::find(object, key)) {
    case variant o::some(found):
        throw (std.json::kind(*found) != std.json::value_kind::string) failure(error_code::malformed);
        return std.json::text(*found);
    case variant o::none: return "";
    }
}

protected bool public_verifies(algorithm alg, const std.crypto::public_key* key, const u8[] input,
                               const u8[] signature) throws std.crypto::crypto_error {
    switch (*key) {
    case variant std.crypto::public_key::ecdsa(point): return point->verify(input, signature);
    case variant std.crypto::public_key::rsa(rsa): return rsa->verify(scheme_of(alg), input, signature);
    case variant std.crypto::public_key::ed25519(edwards): return std.crypto::verify((*edwards)[..], input, signature);
    case variant std.crypto::public_key::x25519(_): return false;
    }
}

/* Whether a verifier's material verifies the signature of the input. */
protected bool verifies(algorithm alg, const verifier* key, const u8[] input, const u8[] signature)
    throws jwt_error, std.alloc::alloc_error {
    try {
        switch (key->key) {
        case variant material::hmac(secret):
            bytes expected = hmac_of(alg, std.secret::as_slice(secret), input);
            return std.secret::constant_time_equal(expected.as_slice(), signature);
        case variant material::private_key(pair):
            std.crypto::public_key public_part = pair->public_key();
            return public_verifies(alg, &public_part, input, signature);
        case variant material::public_key(point): return public_verifies(alg, point, input, signature);
        }
    } catch (std.crypto::crypto_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_key);
}


/* R-SLIB-JWT-0006: the checks of the registered claims after the signature. */
struct validation {
    std.string::string issuer;
    array<std.string::string> audiences;
    u64 leeway = 60u64;
    bool require_expiration = true;
};

/* No issuer or audience to check, 60 seconds of leeway and a required expiration. */
validation validation::create() {
    return validation {.issuer = std.string::create(), .audiences = std.array::create::<std.string::string>(),
                       .leeway = 60u64, .require_expiration = true};
}

/* Requires the `iss` claim to equal `issuer`. */
void validation::set_issuer(validation* this, str issuer) throws std.alloc::alloc_error {
    std.string::string old = core::replace(&this->issuer, std.string::from_str(issuer));
    drop old;
}

/* Accepts tokens whose `aud` claim names `audience`, among others added. */
void validation::add_audience(validation* this, str audience) throws std.alloc::alloc_error {
    try {
        this->audiences.push(std.string::from_str(audience));
    } catch (std.array::push_error<std.string::string> rejected) {
        switch (move rejected) {
        case variant std.array::push_error::allocation_failed(move payload): throw payload.reason;
        }
    }
}

/* The NumericDate of a claim in whole seconds; a fraction is dropped. */
protected i64 seconds_of(const std.json::value* claim) throws jwt_error {
    throw (std.json::kind(claim) != std.json::value_kind::number) failure(error_code::invalid_claim);
    str text = std.json::text(claim);
    const u8[] digits = text;
    usize end = len(digits);
    for (usize index = 0usize; index < len(digits); index += 1usize) {
        if (digits[index] == 46u8 || digits[index] == 101u8 || digits[index] == 69u8) {
            if (end == len(digits)) { end = index; }
        }
    }
    throw (end != len(digits) && digits[end] != 46u8) failure(error_code::invalid_claim);
    try {
        return std.convert::parse_i64(core::validate_utf8(digits[0usize..end]), 10u32);
    } catch (std.convert::parse_error rejected) {
        rejected as void;
    } catch (core::utf8_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_claim);
}

/* Whether the `aud` claim, a string or an array of strings, names one of the audiences. */
protected bool audience_matches(const std.json::value* claim, const array<std.string::string>* audiences)
    throws jwt_error {
    std.json::value_kind kind = std.json::kind(claim);
    if (kind == std.json::value_kind::string) {
        str text = std.json::text(claim);
        const u8[] named = text;
        for (usize index = 0usize; index < len(*audiences); index += 1usize) {
            if (std.bytes::equal((*audiences)[index], named) == true) { return true; }
        }
        return false;
    }
    throw (kind != std.json::value_kind::array) failure(error_code::invalid_claim);
    for (usize item = 0usize; item < std.json::len(claim); item += 1usize) {
        switch (std.json::get(claim, item)) {
        case variant o::some(entry):
            throw (std.json::kind(*entry) != std.json::value_kind::string) failure(error_code::invalid_claim);
            str text = std.json::text(*entry);
            const u8[] named = text;
            for (usize index = 0usize; index < len(*audiences); index += 1usize) {
                if (std.bytes::equal((*audiences)[index], named) == true) { return true; }
            }
        case variant o::none: break;
        }
    }
    return false;
}

/* Checks exp, nbf, iss and aud of the claims at the time `now`. */
protected void check_claims(const std.json::value* claims, const validation* rules, std.time::system_time now)
    throws jwt_error {
    i64 current = now.unix_seconds;
    i64 leeway = rules->leeway as i64;
    switch (std.json::find(claims, "exp")) {
    case variant o::some(found):
        i64 expires = seconds_of(*found);
        throw (current >= expires + leeway) failure(error_code::expired);
    case variant o::none:
        throw (rules->require_expiration == true) failure(error_code::missing_claim);
    }
    switch (std.json::find(claims, "nbf")) {
    case variant o::some(found):
        i64 start = seconds_of(*found);
        throw (current + leeway < start) failure(error_code::not_yet_valid);
    case variant o::none: break;
    }
    const u8[] issuer = rules->issuer;
    if (len(issuer) > 0usize) {
        switch (std.json::find(claims, "iss")) {
        case variant o::some(found):
            throw (std.json::kind(*found) != std.json::value_kind::string) failure(error_code::invalid_issuer);
            str named = std.json::text(*found);
            const u8[] named_bytes = named;
            throw (std.bytes::equal(named_bytes, issuer) == false) failure(error_code::invalid_issuer);
        case variant o::none: throw failure(error_code::invalid_issuer);
        }
    }
    if (len(rules->audiences) > 0usize) {
        switch (std.json::find(claims, "aud")) {
        case variant o::some(found):
            throw (audience_matches(*found, &rules->audiences) == false) failure(error_code::invalid_audience);
        case variant o::none: throw failure(error_code::invalid_audience);
        }
    }
}

/* The payload bytes of a token whose signature verifies with a key of the set. */
protected bytes verified_payload(const key_set* keys, str token) throws jwt_error, std.alloc::alloc_error {
    const u8[] text = token;
    parts at = split(text);
    bytes header_bytes = unbase64url(text[0usize..at.first_dot]);
    std.json::value header_value = parse_object(header_bytes.as_slice());
    switch (std.json::find(&header_value, "crit")) {
    case variant o::some(_): throw failure(error_code::unsupported_algorithm);
    case variant o::none: break;
    }
    str alg_text = string_member(&header_value, "alg");
    const u8[] alg_name = alg_text;
    algorithm alg = algorithm_named(alg_name);
    str kid_text = string_member(&header_value, "kid");
    const u8[] kid = kid_text;
    bytes payload = unbase64url(text[(at.first_dot + 1usize)..at.second_dot]);
    bytes signature = unbase64url(text[(at.second_dot + 1usize)..len(text)]);
    const u8[] input = text[0usize..at.second_dot];
    bool candidate = false;
    for (usize index = 0usize; index < len(keys->keys); index += 1usize) {
        const verifier* key = &keys->keys[index];
        if (key->accepts(alg, kid) == true) {
            candidate = true;
            if (verifies(alg, key, input, signature.as_slice()) == true) { return move payload; }
        }
    }
    throw (candidate == false) failure(error_code::no_key);
    throw failure(error_code::invalid_signature);
}

/* R-SLIB-JWT-0006: the claims of a token whose signature verifies with a key of the set and
   whose registered claims pass the validation at `now`. */
std.json::value verify(const key_set* keys, str token, const validation* rules, std.time::system_time now)
    throws jwt_error, std.alloc::alloc_error {
    bytes payload = verified_payload(keys, token);
    std.json::value claims = parse_object(payload.as_slice());
    check_claims(&claims, rules, now);
    return move claims;
}

/* R-SLIB-JWT-0006: the claims of a verified token read as a value of T by std.json. */
@generic<T: json_decode>
T verify_claims(const key_set* keys, str token, const validation* rules, std.time::system_time now)
    throws jwt_error, std.json::error, std.alloc::alloc_error {
    bytes payload = verified_payload(keys, token);
    std.json::value claims = parse_object(payload.as_slice());
    check_claims(&claims, rules, now);
    T result = std.json::unmarshal(payload.as_slice());
    return move result;
}

/* ---- JSON Web Keys ---- */

/* The bytes of a base64url member of a JWK. */
protected bytes key_member(const std.json::value* entry, str name) throws jwt_error, std.alloc::alloc_error {
    str member = string_member(entry, name);
    const u8[] text = member;
    throw (len(text) == 0usize) failure(error_code::invalid_key);
    return unbase64url(text);
}

/* The verifier of one JWK, or none for a key of a type, curve or use this module does not
   verify with. */
protected o<verifier> verifier_of_jwk(const std.json::value* entry) throws jwt_error, std.alloc::alloc_error {
    throw (std.json::kind(entry) != std.json::value_kind::object) failure(error_code::invalid_key);
    str use_text = string_member(entry, "use");
    const u8[] use = use_text;
    if (len(use) > 0usize && std.bytes::equal(use, "sig") == false) { return o::none; }
    str kty_text = string_member(entry, "kty");
    const u8[] kty = kty_text;
    str crv_text = string_member(entry, "crv");
    const u8[] crv = crv_text;
    str alg_text = string_member(entry, "alg");
    const u8[] alg_name = alg_text;
    o<algorithm> alg = o::none;
    if (len(alg_name) > 0usize) {
        try {
            alg = o::some(algorithm_named(alg_name));
        } catch (jwt_error rejected) {
            rejected as void;
            return o::none;
        }
    }
    material key = material::hmac(std.secret::with_length(0usize));
    try {
        if (std.bytes::equal(kty, "RSA") == true) {
            bytes modulus = key_member(entry, "n");
            bytes exponent = key_member(entry, "e");
            std.crypto::rsa_public_key rsa = std.crypto::rsa_public_key::from_components(modulus.as_slice(),
                                                                                        exponent.as_slice());
            material old = core::replace(&key, material::public_key(std.crypto::public_key::rsa(move rsa)));
            drop old;
        } else {
            if (std.bytes::equal(kty, "EC") == true) {
                std.crypto::curve which = std.crypto::curve::p256;
                if (std.bytes::equal(crv, "P-384") == true) {
                    which = std.crypto::curve::p384;
                } else {
                    if (std.bytes::equal(crv, "P-256") == false) { return o::none; }
                }
                bytes x = key_member(entry, "x");
                bytes y = key_member(entry, "y");
                usize size = which.scalar_length();
                throw (len(x) != size || len(y) != size) failure(error_code::invalid_key);
                bytes point = std.bytes::with_capacity(2usize * size + 1usize);
                std.bytes::append_u8(&point, 4u8);
                std.bytes::append(&point, x.as_slice());
                std.bytes::append(&point, y.as_slice());
                std.crypto::ecdsa_public_key ecdsa = std.crypto::ecdsa_public_key::from_point(which, point.as_slice());
                material old = core::replace(&key, material::public_key(std.crypto::public_key::ecdsa(move ecdsa)));
                drop old;
            } else {
                if (std.bytes::equal(kty, "OKP") == true) {
                    if (std.bytes::equal(crv, "Ed25519") == false) { return o::none; }
                    bytes x = key_member(entry, "x");
                    throw (len(x) != 32usize) failure(error_code::invalid_key);
                    u8[32] raw_key = {};
                    for (usize index = 0usize; index < 32usize; index += 1usize) { raw_key[index] = x[index]; }
                    material old = core::replace(&key, material::public_key(std.crypto::public_key::ed25519(raw_key)));
                    drop old;
                } else {
                    if (std.bytes::equal(kty, "oct") == false) { return o::none; }
                    bytes secret = key_member(entry, "k");
                    material old = core::replace(&key, material::hmac(std.secret::from_bytes(move secret)));
                    drop old;
                }
            }
        }
    } catch (std.crypto::crypto_error rejected) {
        rejected as void;
        throw failure(error_code::invalid_key);
    }
    str kid = string_member(entry, "kid");
    return o::some(verifier {.alg = alg, .kid = std.string::from_str(kid), .key = move key});
}

/* R-SLIB-JWT-0007: the keys of a JWK Set (`{"keys": [...]}`) or of one JWK. Keys of a type or
   curve the module does not verify with, keys whose use is not `sig` and keys with an unknown
   `alg` are skipped; a key of a known type that does not hold a valid key is invalid_key. */
key_set key_set::from_jwks(const u8[] text) throws jwt_error, std.alloc::alloc_error {
    std.json::value document = parse_object(text);
    key_set result = key_set::create();
    switch (std.json::find(&document, "keys")) {
    case variant o::some(keys):
        throw (std.json::kind(*keys) != std.json::value_kind::array) failure(error_code::malformed);
        for (usize index = 0usize; index < std.json::len(*keys); index += 1usize) {
            switch (std.json::get(*keys, index)) {
            case variant o::some(listed):
                o<verifier> found = verifier_of_jwk(*listed);
                switch (move found) {
                case variant o::some(move key): result.add(move key);
                case variant o::none: break;
                }
            case variant o::none: break;
            }
        }
    case variant o::none:
        o<verifier> found = verifier_of_jwk(&document);
        switch (move found) {
        case variant o::some(move key): result.add(move key);
        case variant o::none: break;
        }
    }
    return move result;
}

protected void put_text(std.json::value* object, str name, const u8[] text) throws std.alloc::alloc_error, std.json::error {
    const u8[] key = name;
    std.json::insert(object, key, std.json::from_string(text));
}

protected void put_base64url(std.json::value* object, str name, const u8[] data)
    throws std.alloc::alloc_error, std.json::error {
    std.string::string encoded = base64url(data);
    put_text(object, name, encoded);
}

/* R-SLIB-JWT-0007: the public JWK of a key for its algorithm, with `kid` when it is not empty;
   an X25519 key, which does not sign, is key_mismatch. */
std.json::value jwk(const std.crypto::public_key* key, algorithm alg, str kid) throws jwt_error, std.alloc::alloc_error {
    throw (public_fits(alg, key) == false) failure(error_code::key_mismatch);
    try {
        std.json::value object = std.json::object();
        switch (*key) {
        case variant std.crypto::public_key::ecdsa(point):
            const u8[] encoded = point->point();
            std.crypto::curve which = point->curve();
            usize size = which.scalar_length();
            put_text(&object, "kty", "EC");
            if (which == std.crypto::curve::p384) {
                put_text(&object, "crv", "P-384");
            } else {
                put_text(&object, "crv", "P-256");
            }
            put_base64url(&object, "x", encoded[1usize..(1usize + size)]);
            put_base64url(&object, "y", encoded[(1usize + size)..len(encoded)]);
        case variant std.crypto::public_key::rsa(rsa):
            put_text(&object, "kty", "RSA");
            put_base64url(&object, "n", rsa->modulus());
            put_base64url(&object, "e", rsa->exponent());
        case variant std.crypto::public_key::ed25519(edwards):
            put_text(&object, "kty", "OKP");
            put_text(&object, "crv", "Ed25519");
            put_base64url(&object, "x", (*edwards)[..]);
        case variant std.crypto::public_key::x25519(_): throw failure(error_code::key_mismatch);
        }
        str alg_name = alg.name();
        const u8[] alg_bytes = alg_name;
        put_text(&object, "alg", alg_bytes);
        put_text(&object, "use", "sig");
        const u8[] kid_bytes = kid;
        if (len(kid_bytes) > 0usize) { put_text(&object, "kid", kid_bytes); }
        return move object;
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::malformed);
}
