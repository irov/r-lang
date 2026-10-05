module std.oauth2;
import std.http;
import std.tls;
import std.jwt;
import std.crypto;
import std.encoding;

/* R-SLIB-OAUTH2-0001: the client side of OAuth 2.0 (RFC 6749) for programs that call APIs: token
   requests to a token endpoint, the JWT bearer grant of RFC 7523 for service accounts, and a
   cache of the access token that the tasks of a program share. */

/* R-SLIB-OAUTH2-0001: why a token was not obtained. */
@derive(format)
enum error_code { invalid_request, invalid_response, endpoint_error, invalid_key_file };

/* The status of the endpoint's answer (zero before one), and for endpoint_error the `error` and
   `error_description` of its JSON answer (RFC 6749 section 5.2). */
error oauth2_error {
    error_code code;
    u16 status;
    std.string::string error;
    std.string::string description;
};

protected oauth2_error failure(error_code code, u16 status) throws std.alloc::alloc_error {
    return oauth2_error {.code = code, .status = status, .error = std.string::create(),
                         .description = std.string::create()};
}

/* R-SLIB-OAUTH2-0002: an access token and what the endpoint said about it. Empty strings stand
   for an absent refresh token and scope. */
struct token {
    std.string::string access_token;
    std.string::string token_type;
    o<std.time::system_time> expires_at;
    std.string::string refresh_token;
    std.string::string scope;
};

/* R-SLIB-OAUTH2-0003: the code of an authorization code grant, with its redirect URI and its PKCE
   verifier (RFC 7636), empty strings for none. */
struct code_grant {
    std.string::string code;
    std.string::string redirect_uri;
    std.string::string verifier;
};

/* R-SLIB-OAUTH2-0003: the grant a token request presents. */
enum grant {
    client_credentials,
    refresh_token(std.string::string),
    authorization_code(code_grant),
    jwt_bearer(std.string::string),
};

/* R-SLIB-OAUTH2-0003: a request to a token endpoint. A client identifier with a secret
   authenticates with HTTP Basic (RFC 6749 section 2.3.1); one without a secret is sent in the
   body, as a public client does; an empty identifier sends neither. An empty scope is left out. */
struct token_request {
    std.string::string endpoint;
    std.string::string client_id;
    std.secret::buffer client_secret;
    std.string::string scope;
    grant kind;
};

/* A request to `endpoint` with `kind` and no client authentication or scope. */
token_request token_request::create(str endpoint, grant kind) throws std.alloc::alloc_error {
    return token_request {.endpoint = std.string::from_str(endpoint), .client_id = std.string::create(),
                          .client_secret = std.secret::with_length(0usize), .scope = std.string::create(),
                          .kind = move kind};
}

/* Sets the client identifier and secret; an empty secret makes a public client. */
void token_request::set_client(token_request* this, str client_id, const u8[] secret) throws std.alloc::alloc_error {
    std.string::string old_id = core::replace(&this->client_id, std.string::from_str(client_id));
    drop old_id;
    bytes copied = std.bytes::with_capacity(len(secret));
    std.bytes::append(&copied, secret);
    std.secret::buffer old_secret = core::replace(&this->client_secret, std.secret::from_bytes(move copied));
    drop old_secret;
}

void token_request::set_scope(token_request* this, str scope) throws std.alloc::alloc_error {
    std.string::string old = core::replace(&this->scope, std.string::from_str(scope));
    drop old;
}

protected std.string::string copy_text(const std.string::string* text) throws std.alloc::alloc_error {
    return std.string::from_str(text->as_str());
}

protected grant copy_grant(const grant* kind) throws std.alloc::alloc_error {
    switch (*kind) {
    case variant grant::client_credentials: return grant::client_credentials;
    case variant grant::refresh_token(value): return grant::refresh_token(copy_text(value));
    case variant grant::authorization_code(code):
        return grant::authorization_code(code_grant {.code = copy_text(&code->code),
                                                     .redirect_uri = copy_text(&code->redirect_uri),
                                                     .verifier = copy_text(&code->verifier)});
    case variant grant::jwt_bearer(assertion): return grant::jwt_bearer(copy_text(assertion));
    }
}

protected token_request copy_request(const token_request* request) throws std.alloc::alloc_error {
    bytes secret = std.bytes::with_capacity(std.secret::len(&request->client_secret));
    std.bytes::append(&secret, std.secret::as_slice(&request->client_secret));
    return token_request {.endpoint = copy_text(&request->endpoint), .client_id = copy_text(&request->client_id),
                          .client_secret = std.secret::from_bytes(move secret), .scope = copy_text(&request->scope),
                          .kind = copy_grant(&request->kind)};
}

/* ---- The form of a token request ---- */

protected void add_field(std.string::string* form, str name, str value) throws std.alloc::alloc_error {
    const u8[] written = form->as_bytes();
    if (len(written) > 0usize) { form->append("&"); }
    form->append(name);
    form->append("=");
    std.string::string encoded = std.encoding::percent_encode(value);
    form->append(encoded.as_str());
}

/* The application/x-www-form-urlencoded body of a request. */
protected std.string::string form_of(const token_request* request) throws std.alloc::alloc_error {
    std.string::string form = std.string::create();
    switch (request->kind) {
    case variant grant::client_credentials: add_field(&form, "grant_type", "client_credentials");
    case variant grant::refresh_token(value):
        add_field(&form, "grant_type", "refresh_token");
        add_field(&form, "refresh_token", value->as_str());
    case variant grant::authorization_code(code):
        add_field(&form, "grant_type", "authorization_code");
        add_field(&form, "code", code->code.as_str());
        const u8[] redirect = code->redirect_uri.as_bytes();
        if (len(redirect) > 0usize) { add_field(&form, "redirect_uri", code->redirect_uri.as_str()); }
        const u8[] verifier = code->verifier.as_bytes();
        if (len(verifier) > 0usize) { add_field(&form, "code_verifier", code->verifier.as_str()); }
    case variant grant::jwt_bearer(assertion):
        add_field(&form, "grant_type", "urn:ietf:params:oauth:grant-type:jwt-bearer");
        add_field(&form, "assertion", assertion->as_str());
    }
    const u8[] scope = request->scope.as_bytes();
    if (len(scope) > 0usize) { add_field(&form, "scope", request->scope.as_str()); }
    const u8[] client = request->client_id.as_bytes();
    if (len(client) > 0usize && std.secret::len(&request->client_secret) == 0usize) {
        add_field(&form, "client_id", request->client_id.as_str());
    }
    return move form;
}

/* The value of the Authorization field of a confidential client: Basic with the form-encoded
   identifier and secret. */
protected std.string::string basic_of(const token_request* request) throws oauth2_error, std.alloc::alloc_error {
    std.string::string pair = std.encoding::percent_encode(request->client_id.as_str());
    pair.append(":");
    try {
        str secret = core::validate_utf8(std.secret::as_slice(&request->client_secret));
        std.string::string encoded = std.encoding::percent_encode(secret);
        pair.append(encoded.as_str());
    } catch (core::utf8_error rejected) {
        rejected as void;
        throw failure(error_code::invalid_request, 0u16);
    }
    std.string::string credentials = std.encoding::encode_base64(pair.as_bytes());
    std.string::string value = std.string::from_str("Basic ");
    value.append(credentials.as_str());
    return move value;
}

/* ---- The answer of the endpoint ---- */

protected std.string::string member_text(const std.json::value* object, str name) throws std.alloc::alloc_error {
    const u8[] key = name;
    switch (std.json::find(object, key)) {
    case variant o::some(found):
        if (std.json::kind(*found) == std.json::value_kind::string) { return std.string::from_str(std.json::text(*found)); }
    case variant o::none: break;
    }
    return std.string::create();
}

/* The token of a successful answer (RFC 6749 section 5.1). */
protected token token_of(const u8[] body, u16 status, std.time::system_time now) throws oauth2_error, std.alloc::alloc_error {
    try {
        std.json::value answer = std.json::parse(body);
        throw (std.json::kind(&answer) != std.json::value_kind::object) failure(error_code::invalid_response, status);
        std.string::string access = member_text(&answer, "access_token");
        std.string::string kind = member_text(&answer, "token_type");
        const u8[] access_bytes = access.as_bytes();
        const u8[] kind_bytes = kind.as_bytes();
        throw (len(access_bytes) == 0usize || len(kind_bytes) == 0usize) failure(error_code::invalid_response, status);
        o<std.time::system_time> expires = o::none;
        switch (std.json::find(&answer, "expires_in")) {
        case variant o::some(found):
            if (std.json::kind(*found) == std.json::value_kind::number) {
                try {
                    i64 seconds = std.convert::parse_i64(std.json::text(*found), 10u32);
                    expires = o::some(std.time::system_time {.unix_seconds = now.unix_seconds + seconds,
                                                             .nanoseconds = now.nanoseconds});
                } catch (std.convert::parse_error rejected) {
                    rejected as void;
                }
            }
        case variant o::none: break;
        }
        return token {.access_token = move access, .token_type = move kind, .expires_at = expires,
                      .refresh_token = member_text(&answer, "refresh_token"), .scope = member_text(&answer, "scope")};
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    throw failure(error_code::invalid_response, status);
}

/* The error of an answer that is not a success: endpoint_error with the `error` and
   `error_description` of a JSON error answer, invalid_response for anything else. */
protected oauth2_error error_of(const u8[] body, u16 status) throws std.alloc::alloc_error {
    try {
        std.json::value answer = std.json::parse(body);
        if (std.json::kind(&answer) == std.json::value_kind::object) {
            std.string::string code = member_text(&answer, "error");
            const u8[] code_bytes = code.as_bytes();
            if (len(code_bytes) > 0usize) {
                return oauth2_error {.code = error_code::endpoint_error, .status = status, .error = move code,
                                     .description = member_text(&answer, "error_description")};
            }
        }
    } catch (std.json::error rejected) {
        (move rejected) as void;
    }
    return failure(error_code::invalid_response, status);
}

/* R-SLIB-OAUTH2-0004: sends a token request with POST and reads the token of the answer; `now`
   is the time from which `expires_in` counts. */
@scoped
async token request_token(std.http::client* http, const token_request* request, std.time::system_time now)
    throws oauth2_error, std.http::http_error, std.tls::tls_error, std.error::fault {
    std.http::request message = std.http::request::create(std.http::method::post, "/");
    message.headers.set("Content-Type", "application/x-www-form-urlencoded");
    message.headers.set("Accept", "application/json");
    const u8[] client = request->client_id.as_bytes();
    if (len(client) > 0usize && std.secret::len(&request->client_secret) > 0usize) {
        std.string::string authorization = basic_of(request);
        message.headers.set("Authorization", authorization.as_str());
    }
    std.string::string form = form_of(request);
    const u8[] form_bytes = form.as_bytes();
    bytes body = std.bytes::with_capacity(len(form_bytes));
    std.bytes::append(&body, form.as_bytes());
    bytes old_body = core::replace(&message.body, move body);
    drop old_body;
    std.http::response answer = std.http::response::create(0u16);
    task_scope(1) io {
        std.http::response received = await http->send(move message, request->endpoint.as_str());
        std.http::response old = core::replace(&answer, move received);
        drop old;
    }
    throw (answer.status != 200u16) error_of(answer.body.as_slice(), answer.status);
    return token_of(answer.body.as_slice(), answer.status, now);
}

/* ---- Service accounts ---- */

/* The token endpoint of a key file that names none. */
protected const str google_token_uri = "https://oauth2.googleapis.com/token";

/* R-SLIB-OAUTH2-0005: a service account of a key file: its e-mail, its token endpoint and the
   RS256 signer of its private key, which names the key identifier of the file. */
struct service_account {
    std.string::string client_email;
    std.string::string token_uri;
    protected std.jwt::signer signer;
};

protected std.string::string required_member(const std.json::value* object, str name)
    throws oauth2_error, std.alloc::alloc_error {
    std.string::string text = member_text(object, name);
    const u8[] bytes_of_text = text.as_bytes();
    throw (len(bytes_of_text) == 0usize) failure(error_code::invalid_key_file, 0u16);
    return move text;
}

/* The account of a service account key file in the JSON form of Google Cloud: `type`
   `service_account` when present, `client_email`, `private_key` with an RSA key in PEM,
   `private_key_id` and `token_uri`. */
service_account service_account::from_json(const u8[] text) throws oauth2_error, std.alloc::alloc_error {
    try {
        std.json::value file = std.json::parse(text);
        throw (std.json::kind(&file) != std.json::value_kind::object) failure(error_code::invalid_key_file, 0u16);
        std.string::string kind = member_text(&file, "type");
        const u8[] kind_bytes = kind.as_bytes();
        throw (len(kind_bytes) > 0usize && std.bytes::equal(kind_bytes, "service_account") == false)
            failure(error_code::invalid_key_file, 0u16);
        std.string::string email = required_member(&file, "client_email");
        std.string::string pem = required_member(&file, "private_key");
        std.string::string uri = member_text(&file, "token_uri");
        const u8[] uri_bytes = uri.as_bytes();
        if (len(uri_bytes) == 0usize) { uri.append(google_token_uri); }
        std.crypto::private_key key = std.crypto::private_key::from_pem(pem.as_bytes());
        std.jwt::signer signer = std.jwt::signer::with_private_key(std.jwt::algorithm::rs256, move key);
        std.string::string kid = member_text(&file, "private_key_id");
        signer.set_key_id(kid.as_str());
        return service_account {.client_email = move email, .token_uri = move uri, .signer = move signer};
    } catch (std.json::error rejected) {
        (move rejected) as void;
    } catch (std.crypto::crypto_error rejected) {
        rejected as void;
    } catch (std.jwt::jwt_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_key_file, 0u16);
}

/* The JWT assertion of RFC 7523 section 3 that the account signs for `scope`: issuer the
   account, audience its token endpoint, issued at `now` and valid for one hour, with `sub` the
   user it acts for when `subject` is not empty. */
std.string::string service_account::assertion(const service_account* this, str scope, str subject,
                                              std.time::system_time now) throws oauth2_error, std.alloc::alloc_error {
    try {
        std.json::value claims = std.json::object();
        std.json::insert(&claims, "iss", std.json::from_string(this->client_email.as_bytes()));
        const u8[] scope_bytes = scope;
        if (len(scope_bytes) > 0usize) { std.json::insert(&claims, "scope", std.json::from_string(scope_bytes)); }
        std.json::insert(&claims, "aud", std.json::from_string(this->token_uri.as_bytes()));
        const u8[] subject_bytes = subject;
        if (len(subject_bytes) > 0usize) { std.json::insert(&claims, "sub", std.json::from_string(subject_bytes)); }
        std.string::string issued = f"{now.unix_seconds}";
        std.json::number issued_number = std.json::parse_number(issued.as_bytes());
        std.json::insert(&claims, "iat", std.json::from_number(&issued_number));
        i64 expires = now.unix_seconds + 3600i64;
        std.string::string expiry = f"{expires}";
        std.json::number expiry_number = std.json::parse_number(expiry.as_bytes());
        std.json::insert(&claims, "exp", std.json::from_number(&expiry_number));
        return std.jwt::sign(&this->signer, &claims);
    } catch (std.json::error rejected) {
        (move rejected) as void;
    } catch (std.jwt::jwt_error rejected) {
        rejected as void;
    }
    throw failure(error_code::invalid_key_file, 0u16);
}

/* The token request of the JWT bearer grant with a fresh assertion. */
token_request service_account::request_for(const service_account* this, str scope, str subject,
                                             std.time::system_time now) throws oauth2_error, std.alloc::alloc_error {
    std.string::string assertion = this->assertion(scope, subject, now);
    return token_request::create(this->token_uri.as_str(), grant::jwt_bearer(move assertion));
}

/* ---- The shared token ---- */

/* Where a token source gets its tokens: a stored request, whose refresh token follows the one
   the endpoint returns, or a service account that signs a new assertion each time. */
protected struct account_origin {
    service_account account;
    std.string::string scope;
    std.string::string subject;
};

protected enum origin { request(token_request), account(account_origin) };

protected struct cache {
    std.http::client http;
    origin from;
    std.string::string refresh;
    o<token> current;
};

protected struct source_state { std.async::mutex<cache> slot; };

/* R-SLIB-OAUTH2-0006: an access token that the tasks of a program share: a task that finds the
   token missing or within a minute of its expiry gets a new one while the others wait. */
struct token_source { protected arc source_state core; };

token_source token_source::from_request(std.http::client http, token_request request) throws std.alloc::alloc_error {
    cache held = cache {.http = move http, .from = origin::request(move request), .refresh = std.string::create(),
                       .current = o::none};
    return token_source {.core = new arc source_state {.slot = std.async::mutex_new(move held)}};
}

token_source token_source::from_service_account(std.http::client http, service_account account, str scope,
                                                str subject) throws std.alloc::alloc_error {
    account_origin source = account_origin {.account = move account, .scope = std.string::from_str(scope),
                                            .subject = std.string::from_str(subject)};
    cache held = cache {.http = move http, .from = origin::account(move source), .refresh = std.string::create(),
                       .current = o::none};
    return token_source {.core = new arc source_state {.slot = std.async::mutex_new(move held)}};
}

/* Whether a token is still good a minute from now; one without an expiry stays good. */
protected bool usable(const token* held, std.time::system_time now) {
    switch (held->expires_at) {
    case variant o::some(expires): return expires->unix_seconds - 60i64 > now.unix_seconds;
    case variant o::none: return true;
    }
}

/* The request that the source sends now: a stored request, with the refresh token the endpoint
   returned last in place of its grant, or a fresh assertion of the service account. */
protected token_request request_now(const cache* state, std.time::system_time now) throws oauth2_error, std.alloc::alloc_error {
    switch (state->from) {
    case variant origin::request(stored):
        token_request copied = copy_request(stored);
        const u8[] refresh = state->refresh.as_bytes();
        if (len(refresh) > 0usize) {
            grant old_grant = core::replace(&copied.kind, grant::refresh_token(copy_text(&state->refresh)));
            drop old_grant;
        }
        return move copied;
    case variant origin::account(source):
        return source->account.request_for(source->scope.as_str(), source->subject.as_str(), now);
    }
}

/* Gets a new token for the cache and keeps the refresh token it brings, so that a code is
   exchanged once and a rotated refresh token is followed. */
@scoped
protected async void fill(cache* state, std.time::system_time now)
    throws oauth2_error, std.http::http_error, std.tls::tls_error, std.error::fault {
    token_request request = request_now(state, now);
    token received = token {.access_token = std.string::create(), .token_type = std.string::create(),
                            .expires_at = o::none, .refresh_token = std.string::create(), .scope = std.string::create()};
    task_scope(1) io {
        token answer = await request_token(&state->http, &request, now);
        token old = core::replace(&received, move answer);
        drop old;
    }
    const u8[] refresh = received.refresh_token.as_bytes();
    if (len(refresh) > 0usize) {
        std.string::string old_refresh = core::replace(&state->refresh, copy_text(&received.refresh_token));
        drop old_refresh;
    }
    o<token> old_token = core::replace(&state->current, o::some(move received));
    drop old_token;
}

protected async std.string::string run_access(arc source_state core, bool renew)
    throws oauth2_error, std.http::http_error, std.tls::tls_error, std.error::fault {
    std.async::mutex_guard<cache> guard = await core->slot.lock();
    std.time::system_time now = std.time::system_now();
    if (renew == false) {
        switch ((guard.get())->current) {
        case variant o::some(held):
            if (usable(held, now) == true) { return copy_text(&held->access_token); }
        case variant o::none: break;
        }
    }
    task_scope(1) io { await fill(guard.get_mut(), now); }
    switch ((guard.get())->current) {
    case variant o::some(held): return copy_text(&held->access_token);
    case variant o::none: throw failure(error_code::invalid_response, 0u16);
    }
}

/* The access token: the cached one while it is good, else a new one. */
task<std.string::string throws oauth2_error, std.http::http_error, std.tls::tls_error, std.error::fault>
token_source::access_token(const token_source* this) throws std.async::start_error, std.alloc::alloc_error {
    return run_access(std.arc::clone(&this->core), false);
}

/* A new access token in any case, as after an API answered 401 to the cached one. */
task<std.string::string throws oauth2_error, std.http::http_error, std.tls::tls_error, std.error::fault>
token_source::renew(const token_source* this) throws std.async::start_error, std.alloc::alloc_error {
    return run_access(std.arc::clone(&this->core), true);
}
