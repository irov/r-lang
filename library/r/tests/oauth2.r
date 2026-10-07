module tests.std.oauth2;
import std.test;
import std.http;
import std.tls;
import std.service;
import std.crypto;
import std.encoding;
import std.jwt;
import std.oauth2;
import std.text;

// The tests of std.oauth2 (Library R-SLIB-OAUTH2-0001..0006) against a token endpoint served by
// std.http in the same process: client credentials with HTTP Basic, an error answer, an
// authorization code exchanged once and refreshed with rotation, and a service account whose
// RS256 assertion the endpoint verifies with std.jwt; token sources reuse a good token. Run in
// test mode (Core R-FUNC-0025).

struct Endpoint {
    atomic u32 requests;
    std.jwt::key_set accounts;
    std.string::string audience;
};

/* The value of a field of a form body, empty when it is absent. */
protected std.string::string field(const u8[] form, str name) throws std.alloc::alloc_error {
    const u8[] wanted = name;
    usize start = 0usize;
    for (usize index = 0usize; index <= len(form); index += 1usize) {
        if (index == len(form) || form[index] == 38u8) {
            const u8[] pair = form[start..index];
            usize equals = len(pair);
            for (usize at = 0usize; at < len(pair); at += 1usize) {
                if (pair[at] == 61u8 && equals == len(pair)) { equals = at; }
            }
            if (equals < len(pair) && std.bytes::equal(pair[0usize..equals], wanted) == true) {
                try {
                    str encoded = core::validate_utf8(pair[(equals + 1usize)..len(pair)]);
                    bytes decoded = std.encoding::percent_decode(encoded);
                    return std.string::from_utf8(decoded.as_slice());
                } catch (core::utf8_error rejected) {
                    rejected as void;
                } catch (std.convert::parse_error rejected) {
                    rejected as void;
                } catch (std.string::string_error rejected) {
                    rejected as void;
                }
            }
            start = index + 1usize;
        }
    }
    return std.string::create();
}

protected bool same(const std.string::string* text, str expected) {
    const u8[] wanted = expected;
    return std.bytes::equal(*text, wanted);
}

protected std.http::response answer(str access, u32 count, str refresh, i64 expires_in) throws std.alloc::alloc_error {
    std.string::string body = f"{{\"access_token\":\"{access}-{count}\",\"token_type\":\"Bearer\",\"expires_in\":{expires_in}";
    const u8[] refresh_bytes = refresh;
    if (len(refresh_bytes) > 0usize) {
        std.string::string more = f",\"refresh_token\":\"{refresh}\"";
        body.append(more);
    }
    body.append("}");
    return std.http::response::json(200u16, body);
}

protected std.http::response refusal(str code) throws std.alloc::alloc_error {
    std.string::string body = f"{{\"error\":\"{code}\",\"error_description\":\"refused by the test endpoint\"}}";
    return std.http::response::json(400u16, body);
}

/* The token endpoint of the tests. */
protected async std.http::response token_endpoint(arc Endpoint state, std.http::request incoming)
    throws std.error::fault {
    const Endpoint* shared = &*state;
    u32 count = core::atomic_fetch_add(&shared->requests, 1u32, core::memory_order::relaxed) + 1u32;
    const u8[] form = incoming.body.as_slice();
    std.string::string kind = field(form, "grant_type");
    if (same(&kind, "client_credentials") == true) {
        std.string::string expected = std.string::from_str("Basic ");
        std.string::string pair = std.encoding::encode_base64("client:s%3Acret");
        expected.append(pair);
        switch (incoming.headers.get("Authorization")) {
        case variant o::some(given):
            const u8[] given_bytes = *given;
            if (std.bytes::equal(given_bytes, expected) == false) { return refusal("invalid_client"); }
        case variant o::none: return refusal("invalid_client");
        }
        return answer("cc", count, "", 3600i64);
    }
    if (same(&kind, "authorization_code") == true) {
        std.string::string code = field(form, "code");
        if (same(&code, "abc") == false) { return refusal("invalid_grant"); }
        std.string::string verifier = field(form, "code_verifier");
        if (same(&verifier, "pkce") == false) { return refusal("invalid_grant"); }
        std.string::string client = field(form, "client_id");
        if (same(&client, "app") == false) { return refusal("invalid_client"); }
        return answer("code", count, "r1", 1i64);
    }
    if (same(&kind, "refresh_token") == true) {
        std.string::string refresh = field(form, "refresh_token");
        if (same(&refresh, "r1") == true) { return answer("refreshed", count, "r2", 3600i64); }
        if (same(&refresh, "r2") == true) { return answer("rotated", count, "", 3600i64); }
        return refusal("invalid_grant");
    }
    if (same(&kind, "urn:ietf:params:oauth:grant-type:jwt-bearer") == true) {
        std.string::string assertion = field(form, "assertion");
        std.jwt::validation rules = std.jwt::validation::create();
        rules.set_issuer("robot@example.test");
        rules.add_audience(shared->audience);
        try {
            std.json::value claims = std.jwt::verify(&shared->accounts, assertion, &rules, std.time::system_now());
            switch (std.json::find(&claims, "scope")) {
            case variant o::some(scope):
                str scope_text = std.json::text(*scope);
                const u8[] scope_bytes = scope_text;
                if (std.bytes::equal(scope_bytes, "scope.read") == false) { return refusal("invalid_scope"); }
            case variant o::none: return refusal("invalid_scope");
            }
        } catch (std.jwt::jwt_error rejected) {
            rejected as void;
            return refusal("invalid_grant");
        }
        return answer("sa", count, "", 3600i64);
    }
    return refusal("unsupported_grant_type");
}

protected async std.net::tcp_listener open_listener() throws std.error::fault {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16, .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 16u32, .reuse_address = true, .v6_only = false};
    return await local.listen(options);
}

protected bool starts(const std.string::string* text, str prefix) {
    const u8[] wanted = prefix;
    return std.bytes::starts_with(*text, wanted);
}

/* One task's access token from a source that several tasks share. */
protected async std.string::string shared_token(arc std.oauth2::token_source tokens)
    throws std.oauth2::oauth2_error, std.http::http_error, std.tls::tls_error, std.error::fault {
    return await std.oauth2::token_source::access_token(&*tokens);
}

/* The requests of the client side, then a drain of the server. */
protected async u32 clients(std.string::string endpoint, std.string::string key_text,
                            std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.test::failure, std.oauth2::oauth2_error, std.http::http_error, std.tls::tls_error {
    std.http::client_options options = {};
    std.http::client web = std.http::client::create(options);
    u32 checked = 0u32;
    std.time::system_time now = std.time::system_now();
    // Client credentials with a secret that HTTP Basic carries form-encoded.
    std.oauth2::token_request plain = std.oauth2::token_request::create(endpoint, std.oauth2::grant::client_credentials);
    plain.set_client("client", "s:cret");
    plain.set_scope("scope.read");
    task_scope(1) io {
        std.oauth2::token first = await std.oauth2::request_token(&web, &plain, now);
        std.test::check(same(&first.access_token, "cc-1"), "client credentials token");
        std.test::check(same(&first.token_type, "Bearer"), "token type");
        switch (first.expires_at) {
        case variant o::some(expires): std.test::equal(expires->unix_seconds, now.unix_seconds + 3600i64);
        case variant o::none: std.test::fail("an expiry");
        }
        checked += 1u32;
    }
    // A code the endpoint does not know is its error, with status 400.
    std.oauth2::code_grant bad_code = {.code = std.string::from_str("zzz"), .redirect_uri = std.string::create(),
                                       .verifier = std.string::from_str("pkce")};
    std.oauth2::token_request refused = std.oauth2::token_request::create(endpoint,
                                                                         std.oauth2::grant::authorization_code(move bad_code));
    refused.set_client("app", "");
    try {
        task_scope(1) io {
            std.oauth2::token unexpected = await std.oauth2::request_token(&web, &refused, now);
            drop unexpected;
        }
        std.test::fail("an unknown code");
    } catch (std.oauth2::oauth2_error failure) {
        std.test::check(failure.code == std.oauth2::error_code::endpoint_error, "endpoint_error");
        std.test::equal(failure.status, 400u16);
        std.test::check(same(&failure.error, "invalid_grant"), "invalid_grant");
        checked += 1u32;
    }
    // An authorization code is exchanged once; its short token is refreshed, the refresh token
    // rotates, and a good token is reused.
    std.oauth2::code_grant code = {.code = std.string::from_str("abc"), .redirect_uri = std.string::create(),
                                   .verifier = std.string::from_str("pkce")};
    std.oauth2::token_request exchange = std.oauth2::token_request::create(endpoint,
                                                                          std.oauth2::grant::authorization_code(move code));
    exchange.set_client("app", "");
    std.oauth2::token_source tokens = std.oauth2::token_source::from_request(move web, move exchange);
    std.string::string first_token = await tokens.access_token();
    std.test::check(same(&first_token, "code-3"), "code token");
    std.string::string second_token = await tokens.access_token();
    std.test::check(same(&second_token, "refreshed-4"), "refreshed token");
    std.string::string third_token = await tokens.access_token();
    std.test::check(same(&third_token, "refreshed-4"), "reused token");
    std.string::string fourth_token = await tokens.renew();
    std.test::check(same(&fourth_token, "rotated-5"), "rotated token");
    checked += 1u32;
    // A service account signs an assertion the endpoint verifies; its token is reused.
    std.oauth2::service_account account = std.oauth2::service_account::from_json(key_text);
    std.test::check(same(&account.client_email, "robot@example.test"), "client_email");
    std.test::check(same(&account.token_uri, endpoint), "token_uri");
    std.http::client account_web = std.http::client::create(options);
    std.oauth2::token_source robot = std.oauth2::token_source::from_service_account(move account_web, move account,
                                                                                   "scope.read", "");
    std.string::string robot_token = await robot.access_token();
    std.test::check(same(&robot_token, "sa-6"), "service account token");
    std.string::string robot_again = await robot.access_token();
    std.test::check(same(&robot_again, "sa-6"), "reused service account token");
    checked += 1u32;
    // Two tasks that need a token at once from a shared source send one request.
    std.oauth2::service_account second_account = std.oauth2::service_account::from_json(key_text);
    std.http::client shared_web = std.http::client::create(options);
    std.oauth2::token_source fresh = std.oauth2::token_source::from_service_account(move shared_web, move second_account,
                                                                                   "scope.read", "");
    arc std.oauth2::token_source shared = new arc std.oauth2::token_source(move fresh);
    task_scope(2) pair {
        auto one = shared_token(std.arc::clone(&shared));
        auto two = shared_token(std.arc::clone(&shared));
        std.string::string first_shared = await move one;
        std.string::string second_shared = await move two;
        std.test::check(same(&first_shared, "sa-7"), "one request for both tasks");
        std.test::check(same(&second_shared, "sa-7"), "the same token for both tasks");
        checked += 1u32;
    }
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return checked;
}

/* A key file in the form of Google Cloud for a new RSA key. */
protected std.string::string key_file(const std.crypto::private_key* key, str token_uri)
    throws std.alloc::alloc_error, std.json::error {
    std.secret::buffer pem = key->to_pem();
    std.json::value file = std.json::object();
    std.json::insert(&file, "type", std.json::from_string("service_account"));
    std.json::insert(&file, "client_email", std.json::from_string("robot@example.test"));
    std.json::insert(&file, "private_key_id", std.json::from_string("key-1"));
    std.json::insert(&file, "private_key", std.json::from_string(std.secret::as_slice(&pem)));
    const u8[] uri = token_uri;
    std.json::insert(&file, "token_uri", std.json::from_string(uri));
    return std.json::stringify(&file);
}

@test
async void obtains_and_reuses_tokens()
    throws std.error::fault, std.test::failure, std.oauth2::oauth2_error, std.http::http_error, std.tls::tls_error,
    std.crypto::crypto_error, std.jwt::jwt_error, std.json::error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    u16 port = endpoint.port;
    std.string::string token_uri = f"http://localhost:{port}/token";
    std.crypto::rsa_key rsa = std.crypto::rsa_key::generate(2048usize);
    std.crypto::private_key key = std.crypto::private_key::rsa(move rsa);
    std.crypto::public_key public_part = key.public_key();
    std.string::string file = key_file(&key, token_uri);
    std.jwt::key_set accounts = std.jwt::key_set::create();
    std.jwt::verifier check = std.jwt::verifier::with_public_key(std.jwt::algorithm::rs256, move public_part);
    check.set_key_id("key-1");
    accounts.add(move check);
    arc Endpoint state = new arc Endpoint {.requests = 0u32, .accounts = move accounts,
                                           .audience = std.string::from_str(token_uri)};
    std.http::router<Endpoint> routes = std.http::router<Endpoint>::create();
    routes.add(std.http::method::post, "/token", token_endpoint);
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    std.service::options settings = {};
    std.http::limits bounds = {};
    task_scope(2) group {
        auto server = std.http::serve(move listener, settings, move stop, move state, move routes, bounds);
        auto client = clients(move token_uri, move file, move stopper);
        u32 checked = await move client;
        std.service::report served = await move server;
        std.test::equal(checked, 5u32);
        std.test::equal(served.failed, 0u64);
    }
}

@test
void reads_and_refuses_key_files() throws std.test::failure, std.alloc::alloc_error {
    try {
        std.oauth2::service_account account = std.oauth2::service_account::from_json("{\"type\":\"authorized_user\"}");
        drop account;
        std.test::fail("another type of key file");
    } catch (std.oauth2::oauth2_error failure) {
        std.test::check(failure.code == std.oauth2::error_code::invalid_key_file, "invalid_key_file");
    }
    try {
        std.oauth2::service_account account = std.oauth2::service_account::from_json(
            "{\"client_email\":\"a@b\",\"private_key\":\"-----BEGIN PRIVATE KEY-----\\nAAAA\\n-----END PRIVATE KEY-----\\n\"}");
        drop account;
        std.test::fail("a key that does not parse");
    } catch (std.oauth2::oauth2_error failure) {
        std.test::check(failure.code == std.oauth2::error_code::invalid_key_file, "invalid_key_file");
    }
}
