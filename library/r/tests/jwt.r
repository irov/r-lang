module tests.std.jwt;
import std.test;
import std.crypto;
import std.encoding;
import std.jwt;

// The tests of std.jwt (Library R-SLIB-JWT-0001..0007): the HS256 and ES256 tokens of RFC 7515
// appendices A.1 and A.3 with their JWKs, tokens of every algorithm family made and verified
// here, the registered claims, the choice of a key by `kid` and algorithm, typed claims and
// JWK export. Run in test mode (Core R-FUNC-0025).

protected const str rfc_payload_time = "1300819380";

protected std.time::system_time at(i64 seconds) {
    return std.time::system_time {.unix_seconds = seconds, .nanoseconds = 0u32};
}

protected std.jwt::validation lenient() {
    std.jwt::validation rules = std.jwt::validation::create();
    rules.leeway = 0u64;
    return move rules;
}

@test
void verifies_the_tokens_of_rfc_7515() throws std.test::failure, std.alloc::alloc_error, std.jwt::jwt_error {
    std.jwt::key_set hmac_keys = std.jwt::key_set::from_jwks(
        "{\"kty\":\"oct\",\"k\":\"AyM1SysPpbyDfgZld3umj1qzKObwVMkoqQ-EstJQLr_T-1qS0gZH75aKtMN3Yj0iPS4hcgUuTwjAzZr1Z9CAow\"}");
    std.test::equal(hmac_keys.size(), 1usize);
    str hs256 = "eyJ0eXAiOiJKV1QiLA0KICJhbGciOiJIUzI1NiJ9.eyJpc3MiOiJqb2UiLA0KICJleHAiOjEzMDA4MTkzODAsDQogImh0dHA6Ly9leGFtcGxlLmNvbS9pc19yb290Ijp0cnVlfQ.dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    std.jwt::validation rules = lenient();
    std.json::value claims = std.jwt::verify(&hmac_keys, hs256, &rules, at(1300819000i64));
    switch (std.json::find(&claims, "iss")) {
    case variant o::some(issuer):
        str name = std.json::text(*issuer);
        std.test::equal_text(name, "joe");
    case variant o::none: std.test::fail("an issuer");
    }
    try {
        std.json::value late = std.jwt::verify(&hmac_keys, hs256, &rules, at(1300819380i64));
        drop late;
        std.test::fail("an expired token");
    } catch (std.jwt::jwt_error failure) {
        std.test::check(failure.code == std.jwt::error_code::expired, "expired");
    }
    std.jwt::key_set ec_keys = std.jwt::key_set::from_jwks(
        "{\"keys\":[{\"kty\":\"EC\",\"crv\":\"P-256\",\"x\":\"f83OJ3D2xF1Bg8vub9tLe1gHMzV76e8Tus9uPHvRVEU\",\"y\":\"x_FEzRu9m36HLN_tue659LNpXW6pCyStikYjKIWI5a0\"},{\"kty\":\"unknown\"}]}");
    std.test::equal(ec_keys.size(), 1usize);
    str es256 = "eyJhbGciOiJFUzI1NiJ9.eyJpc3MiOiJqb2UiLA0KICJleHAiOjEzMDA4MTkzODAsDQogImh0dHA6Ly9leGFtcGxlLmNvbS9pc19yb290Ijp0cnVlfQ.DtEhU3ljbEg8L38VWAfUAqOyKAM6-Xx-F4GawxaepmXFCgfTjDxw5djxLa8ISlSApmWQxfKTUJqPP3-Kg6NU1Q";
    std.json::value ec_claims = std.jwt::verify(&ec_keys, es256, &rules, at(1300819000i64));
    std.test::equal(std.json::len(&ec_claims), 3usize);
    try {
        std.json::value crossed = std.jwt::verify(&ec_keys, hs256, &rules, at(1300819000i64));
        drop crossed;
        std.test::fail("an HS256 token under an EC key");
    } catch (std.jwt::jwt_error failure) {
        std.test::check(failure.code == std.jwt::error_code::no_key, "no_key");
    }
}

/* A token of `alg` signed by `key` and verified by `check`; true when it verifies. */
protected bool round_trip(std.jwt::signer key, std.jwt::verifier check)
    throws std.test::failure, std.alloc::alloc_error, std.jwt::jwt_error, std.json::error, std.time::time_error {
    std.json::value claims = std.json::object();
    std.json::insert(&claims, "sub", std.json::from_string("player-7"));
    std.json::number expiry = std.json::parse_number("4102444800");
    std.json::insert(&claims, "exp", std.json::from_number(&expiry));
    std.string::string token = std.jwt::sign(&key, &claims);
    std.jwt::key_set keys = std.jwt::key_set::create();
    keys.add(move check);
    std.jwt::validation rules = std.jwt::validation::create();
    std.json::value read = std.jwt::verify(&keys, token.as_str(), &rules, std.time::system_now());
    return std.json::len(&read) == 2usize;
}

@test
void signs_and_verifies_every_family() throws std.test::failure, std.alloc::alloc_error, std.jwt::jwt_error,
    std.crypto::crypto_error, std.json::error, std.time::time_error {
    str secret = "0123456789abcdef0123456789abcdef";
    std.test::check(round_trip(std.jwt::signer::hmac(std.jwt::algorithm::hs256, secret),
                               std.jwt::verifier::hmac(std.jwt::algorithm::hs256, secret)), "HS256");
    std.crypto::ecdsa_key ec = std.crypto::ecdsa_key::generate(std.crypto::curve::p384);
    std.crypto::private_key ec_key = std.crypto::private_key::ecdsa(move ec);
    std.crypto::public_key ec_public = ec_key.public_key();
    std.test::check(round_trip(std.jwt::signer::with_private_key(std.jwt::algorithm::es384, move ec_key),
                               std.jwt::verifier::with_public_key(std.jwt::algorithm::es384, move ec_public)), "ES384");
    std.crypto::rsa_key rsa = std.crypto::rsa_key::generate(2048usize);
    std.crypto::private_key rsa_key = std.crypto::private_key::rsa(move rsa);
    std.crypto::public_key rsa_public = rsa_key.public_key();
    std.test::check(round_trip(std.jwt::signer::with_private_key(std.jwt::algorithm::ps256, move rsa_key),
                               std.jwt::verifier::with_public_key(std.jwt::algorithm::ps256, move rsa_public)), "PS256");
    std.crypto::signing_key edwards = std.crypto::signing_key::generate();
    std.crypto::private_key edwards_key = std.crypto::private_key::ed25519(move edwards);
    std.crypto::public_key edwards_public = edwards_key.public_key();
    std.test::check(round_trip(std.jwt::signer::with_private_key(std.jwt::algorithm::eddsa, move edwards_key),
                               std.jwt::verifier::with_public_key(std.jwt::algorithm::eddsa, move edwards_public)), "EdDSA");
    try {
        std.jwt::signer short_secret = std.jwt::signer::hmac(std.jwt::algorithm::hs256, "short");
        drop short_secret;
        std.test::fail("an HMAC secret shorter than the hash");
    } catch (std.jwt::jwt_error failure) {
        std.test::check(failure.code == std.jwt::error_code::key_mismatch, "key_mismatch");
    }
    std.crypto::ecdsa_key p256 = std.crypto::ecdsa_key::generate(std.crypto::curve::p256);
    try {
        std.jwt::signer wrong = std.jwt::signer::with_private_key(std.jwt::algorithm::es384, std.crypto::private_key::ecdsa(move p256));
        drop wrong;
        std.test::fail("an ES384 signer with a P-256 key");
    } catch (std.jwt::jwt_error failure) {
        std.test::check(failure.code == std.jwt::error_code::key_mismatch, "curve mismatch");
    }
}

/* The token of the claims under a fixed HS256 secret. */
protected std.string::string issued(const std.json::value* claims) throws std.alloc::alloc_error, std.jwt::jwt_error {
    std.jwt::signer key = std.jwt::signer::hmac(std.jwt::algorithm::hs256, "0123456789abcdef0123456789abcdef");
    return std.jwt::sign(&key, claims);
}

protected std.jwt::error_code outcome(str token, const std.jwt::validation* rules, i64 now)
    throws std.alloc::alloc_error, std.jwt::jwt_error {
    std.jwt::key_set keys = std.jwt::key_set::create();
    keys.add(std.jwt::verifier::hmac(std.jwt::algorithm::hs256, "0123456789abcdef0123456789abcdef"));
    try {
        std.json::value claims = std.jwt::verify(&keys, token, rules, at(now));
        drop claims;
    } catch (std.jwt::jwt_error failure) {
        return failure.code;
    }
    return std.jwt::error_code::invalid_key;
}

protected void put_number(std.json::value* object, str name, str number) throws std.alloc::alloc_error, std.json::error {
    const u8[] key = name;
    const u8[] digits = number;
    std.json::number parsed = std.json::parse_number(digits);
    std.json::insert(object, key, std.json::from_number(&parsed));
}

@test
void checks_the_registered_claims() throws std.test::failure, std.alloc::alloc_error, std.jwt::jwt_error, std.json::error {
    std.json::value claims = std.json::object();
    std.json::insert(&claims, "iss", std.json::from_string("arena"));
    std.json::value audiences = std.json::array();
    std.json::append(&audiences, std.json::from_string("game"));
    std.json::append(&audiences, std.json::from_string("admin"));
    std.json::insert(&claims, "aud", move audiences);
    put_number(&claims, "nbf", "1000");
    put_number(&claims, "exp", "2000");
    std.string::string token = issued(&claims);
    std.jwt::validation rules = std.jwt::validation::create();
    rules.set_issuer("arena");
    rules.add_audience("admin");
    std.test::check(outcome(token.as_str(), &rules, 1500i64) == std.jwt::error_code::invalid_key, "valid");
    std.test::check(outcome(token.as_str(), &rules, 2059i64) == std.jwt::error_code::invalid_key, "within leeway");
    std.test::check(outcome(token.as_str(), &rules, 2060i64) == std.jwt::error_code::expired, "expired");
    std.test::check(outcome(token.as_str(), &rules, 939i64) == std.jwt::error_code::not_yet_valid, "not yet valid");
    std.jwt::validation other_issuer = std.jwt::validation::create();
    other_issuer.set_issuer("someone");
    std.test::check(outcome(token.as_str(), &other_issuer, 1500i64) == std.jwt::error_code::invalid_issuer, "issuer");
    std.jwt::validation other_audience = std.jwt::validation::create();
    other_audience.add_audience("billing");
    std.test::check(outcome(token.as_str(), &other_audience, 1500i64) == std.jwt::error_code::invalid_audience,
                    "audience");
    std.json::value open = std.json::object();
    std.json::insert(&open, "sub", std.json::from_string("x"));
    std.string::string forever = issued(&open);
    std.jwt::validation required = std.jwt::validation::create();
    std.test::check(outcome(forever.as_str(), &required, 1500i64) == std.jwt::error_code::missing_claim, "missing exp");
    required.require_expiration = false;
    std.test::check(outcome(forever.as_str(), &required, 1500i64) == std.jwt::error_code::invalid_key, "no exp needed");
    // A changed payload, an `alg` of none, a malformed token and a forged signature.
    std.string::string changed = std.string::from_str(token.as_str());
    changed.append("x");
    std.test::check(outcome(changed.as_str(), &rules, 1500i64) != std.jwt::error_code::invalid_key, "changed");
    std.test::check(outcome("eyJhbGciOiJub25lIn0.eyJzdWIiOiJ4In0.", &required, 1500i64) ==
                        std.jwt::error_code::unsupported_algorithm, "alg none");
    std.test::check(outcome("not a token", &required, 1500i64) == std.jwt::error_code::malformed, "malformed");
    std.test::check(outcome("eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiJ4In0.AAAA", &required, 1500i64) ==
                        std.jwt::error_code::invalid_signature, "forged");
}

struct Session {
    @json(name = "AccountID") std.string::string account;
    @json(name = "NickName") std.string::string nick;
    u64 exp;
};

@test
void signs_typed_claims_and_chooses_keys_by_kid() throws std.test::failure, std.alloc::alloc_error, std.jwt::jwt_error,
    std.crypto::crypto_error, std.json::error, std.time::time_error {
    std.crypto::ecdsa_key first = std.crypto::ecdsa_key::generate(std.crypto::curve::p256);
    std.crypto::ecdsa_key second = std.crypto::ecdsa_key::generate(std.crypto::curve::p256);
    std.crypto::private_key first_key = std.crypto::private_key::ecdsa(move first);
    std.crypto::private_key second_key = std.crypto::private_key::ecdsa(move second);
    std.crypto::public_key first_public = first_key.public_key();
    std.crypto::public_key second_public = second_key.public_key();
    std.json::value published = std.json::object();
    std.json::value keys_array = std.json::array();
    std.json::append(&keys_array, std.jwt::jwk(&first_public, std.jwt::algorithm::es256, "one"));
    std.json::append(&keys_array, std.jwt::jwk(&second_public, std.jwt::algorithm::es256, "two"));
    std.json::insert(&published, "keys", move keys_array);
    std.string::string jwks = std.json::stringify(&published);
    std.jwt::key_set keys = std.jwt::key_set::from_jwks(jwks.as_bytes());
    std.test::equal(keys.size(), 2usize);
    std.jwt::signer signer = std.jwt::signer::with_private_key(std.jwt::algorithm::es256, move second_key);
    signer.set_key_id("two");
    Session session = {.account = std.string::from_str("42"), .nick = std.string::from_str("Ann"), .exp = 4102444800u64};
    std.string::string token = std.jwt::sign_claims(&signer, &session);
    std.json::value head = std.jwt::header(token.as_str());
    switch (std.json::find(&head, "kid")) {
    case variant o::some(kid):
        str named = std.json::text(*kid);
        std.test::equal_text(named, "two");
    case variant o::none: std.test::fail("a kid");
    }
    std.jwt::validation rules = std.jwt::validation::create();
    Session read = std.jwt::verify_claims::<Session>(&keys, token.as_str(), &rules, std.time::system_now());
    std.test::equal_text(read.account.as_str(), "42");
    std.test::equal_text(read.nick.as_str(), "Ann");
    signer.set_key_id("three");
    std.string::string unknown = std.jwt::sign_claims(&signer, &session);
    try {
        Session refused = std.jwt::verify_claims::<Session>(&keys, unknown.as_str(), &rules, std.time::system_now());
        drop refused;
        std.test::fail("a kid that no key has");
    } catch (std.jwt::jwt_error failure) {
        std.test::check(failure.code == std.jwt::error_code::no_key, "no_key");
    }
    drop first_key;
}
