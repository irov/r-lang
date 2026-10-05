module example.arena.sessions;
import std.crypto;
import std.jwt;

/* The session tokens of the game server: ES256 tokens (ECDSA on P-256) without an expiry, whose
   claims are the account fields the clients read. The key is the server's private key in PEM,
   PKCS#8 or the SEC1 form (`EC PRIVATE KEY`). */

struct Session {
    @json(name = "AccountID") std.string::string account;
    @json(name = "NickName") std.string::string nick;
    @json(name = "Email") std.string::string email;
    @json(name = "Platform") std.string::string platform;
    @json(name = "Provider") std.string::string provider;
    @json(name = "Avatar") std.string::string avatar;
};

/* The key identifier the server names in its tokens and its JWK Set. */
const str key_id = "arena-1";

/* The signer of a PEM private key. */
std.jwt::signer signer_of(const u8[] pem) throws std.crypto::crypto_error, std.jwt::jwt_error, std.alloc::alloc_error {
    std.crypto::private_key key = std.crypto::private_key::from_pem(pem);
    std.jwt::signer signer = std.jwt::signer::with_private_key(std.jwt::algorithm::es256, move key);
    signer.set_key_id(key_id);
    return move signer;
}

/* The keys that verify the tokens of a PEM private key. */
std.jwt::key_set keys_of(const u8[] pem) throws std.crypto::crypto_error, std.jwt::jwt_error, std.alloc::alloc_error {
    std.crypto::private_key key = std.crypto::private_key::from_pem(pem);
    std.crypto::public_key public_part = key.public_key();
    std.jwt::verifier verifying = std.jwt::verifier::with_public_key(std.jwt::algorithm::es256, move public_part);
    verifying.set_key_id(key_id);
    std.jwt::key_set keys = std.jwt::key_set::create();
    keys.add(move verifying);
    return move keys;
}

/* The token of a session. */
std.string::string issue(const u8[] pem, const Session* session)
    throws std.crypto::crypto_error, std.jwt::jwt_error, std.alloc::alloc_error {
    std.jwt::signer signer = signer_of(pem);
    return std.jwt::sign_claims(&signer, session);
}

/* The session of a token the key signed; the tokens have no expiry. */
Session check(const u8[] pem, str token) throws std.crypto::crypto_error, std.jwt::jwt_error, std.json::error,
    std.time::time_error, std.alloc::alloc_error {
    std.jwt::key_set keys = keys_of(pem);
    std.jwt::validation rules = std.jwt::validation::create();
    rules.require_expiration = false;
    return std.jwt::verify_claims::<Session>(&keys, token, &rules, std.time::system_now());
}

/* The algorithm and key identifier of a token, read before verification. */
std.string::string describe(str token) throws std.jwt::jwt_error, std.alloc::alloc_error {
    std.json::value head = std.jwt::header(token);
    std.string::string alg = std.string::create();
    std.string::string kid = std.string::create();
    switch (std.json::find(&head, "alg")) {
    case variant o::some(found): alg.append(std.json::text(*found));
    case variant o::none: break;
    }
    switch (std.json::find(&head, "kid")) {
    case variant o::some(found): kid.append(std.json::text(*found));
    case variant o::none: break;
    }
    return f"alg {alg} kid {kid}";
}

/* The JWK Set of the server's public key, which other services fetch to verify its tokens. */
std.string::string jwks(const u8[] pem) throws std.crypto::crypto_error, std.jwt::jwt_error, std.json::error,
    std.alloc::alloc_error {
    std.crypto::private_key key = std.crypto::private_key::from_pem(pem);
    std.crypto::public_key public_part = key.public_key();
    std.json::value set = std.json::object();
    std.json::value keys = std.json::array();
    std.json::append(&keys, std.jwt::jwk(&public_part, std.jwt::algorithm::es256, key_id));
    std.json::insert(&set, "keys", move keys);
    return std.json::stringify(&set);
}

/* The claims of a token that an administrator signed with a shared HS256 secret; a different
   secret, a stale token or another issuer is refused. */
std.string::string admin(str secret, str token, std.time::system_time now)
    throws std.jwt::jwt_error, std.json::error, std.alloc::alloc_error {
    std.jwt::key_set keys = std.jwt::key_set::create();
    keys.add(std.jwt::verifier::hmac(std.jwt::algorithm::hs256, secret));
    std.jwt::validation rules = std.jwt::validation::create();
    rules.set_issuer("arena-admin");
    std.json::value claims = std.jwt::verify(&keys, token, &rules, now);
    return std.json::stringify(&claims);
}

/* An administrator token for `subject` valid for `seconds` from `now`. */
std.string::string admin_token(str secret, str subject, std.time::system_time now, i64 seconds)
    throws std.jwt::jwt_error, std.json::error, std.alloc::alloc_error {
    std.jwt::signer signer = std.jwt::signer::hmac(std.jwt::algorithm::hs256, secret);
    std.json::value claims = std.json::object();
    std.json::insert(&claims, "iss", std.json::from_string("arena-admin"));
    const u8[] subject_bytes = subject;
    std.json::insert(&claims, "sub", std.json::from_string(subject_bytes));
    i64 expires = now.unix_seconds + seconds;
    std.string::string expires_text = f"{expires}";
    std.json::number number = std.json::parse_number(expires_text.as_bytes());
    std.json::insert(&claims, "exp", std.json::from_number(&number));
    return std.jwt::sign(&signer, &claims);
}
