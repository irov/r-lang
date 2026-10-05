module example.arena.google;
import std.http;
import std.tls;
import std.oauth2;

/* Access tokens for the APIs the game server calls (Play purchases, Sheets) and for its admin
   panel: a service account key file signs an RS256 assertion for the token endpoint the file
   names, a confidential client presents its secret, and a player's login exchanges a code once
   and refreshes it. Token sources keep a token for the tasks of the server until it nears its
   expiry. Tests give a local token endpoint. */

protected std.http::client web() throws std.alloc::alloc_error {
    std.http::client_options options = {};
    return std.http::client::create(options);
}

/* Three requests through one service-account source: the second finds the token in the cache,
   the third renews it. */
async std.string::string service_tokens(std.string::string key_file, std.string::string scope)
    throws std.oauth2::oauth2_error, std.http::http_error, std.tls::tls_error, std.error::fault {
    std.oauth2::service_account account = std.oauth2::service_account::from_json(key_file.as_bytes());
    std.string::string email = std.string::from_str(account.client_email.as_str());
    std.oauth2::token_source tokens = std.oauth2::token_source::from_service_account(web(), move account,
                                                                                    scope.as_str(), "");
    std.string::string first = await tokens.access_token();
    std.string::string second = await tokens.access_token();
    std.string::string renewed = await tokens.renew();
    return f"account {email}\nfirst {first}\nagain {second}\nrenewed {renewed}\n";
}

/* One client-credentials request, or the endpoint's refusal. */
async std.string::string client_token(std.string::string endpoint, std.string::string client, std.string::string secret)
    throws std.http::http_error, std.tls::tls_error, std.error::fault {
    std.http::client http = web();
    std.oauth2::token_request request = std.oauth2::token_request::create(endpoint.as_str(),
                                                                         std.oauth2::grant::client_credentials);
    request.set_client(client.as_str(), secret.as_bytes());
    request.set_scope("admin");
    std.time::system_time now = std.time::system_now();
    std.string::string out = std.string::create();
    try {
        task_scope(1) io {
            std.oauth2::token received = await std.oauth2::request_token(&http, &request, now);
            i64 lifetime = 0i64;
            switch (received.expires_at) {
            case variant o::some(expires): lifetime += expires->unix_seconds - now.unix_seconds;
            case variant o::none: break;
            }
            str access = received.access_token.as_str();
            str kind = received.token_type.as_str();
            std.string::string line = f"token {access} type {kind} expires in {lifetime}\n";
            out.append(line.as_str());
        }
    } catch (std.oauth2::oauth2_error failure) {
        std.oauth2::error_code code = failure.code;
        u16 status = failure.status;
        str reason = failure.error.as_str();
        std.string::string line = f"refused: {code} {status} {reason}\n";
        out.append(line.as_str());
    }
    return move out;
}

/* A player's login: the code is exchanged once, its short token refreshed, then renewed. */
async std.string::string login(std.string::string endpoint, std.string::string code, std.string::string verifier)
    throws std.oauth2::oauth2_error, std.http::http_error, std.tls::tls_error, std.error::fault {
    std.oauth2::code_grant grant = {.code = move code, .redirect_uri = std.string::from_str("arena://login"),
                                    .verifier = move verifier};
    std.oauth2::token_request request = std.oauth2::token_request::create(endpoint.as_str(),
                                                                         std.oauth2::grant::authorization_code(move grant));
    request.set_client("arena-app", "");
    std.oauth2::token_source tokens = std.oauth2::token_source::from_request(web(), move request);
    std.string::string first = await tokens.access_token();
    std.string::string second = await tokens.access_token();
    std.string::string third = await tokens.access_token();
    std.string::string renewed = await tokens.renew();
    return f"code {first}\nrefreshed {second}\ncached {third}\nrenewed {renewed}\n";
}
