module example.arena.server;
import std.console;
import std.crypto;
import std.http;
import std.json;
import std.jwt;
import std.log;
import std.net;
import std.pool;
import std.postgres;
import std.service;
import std.text;
import std.time;
import std.uuid;
import example.arena.sessions;

/* The HTTP side of the game server as an application of std.http (Library R-SLIB-HTTP-0015..0018).
   Its middleware gives each request an identifier and a logger and writes one record of it to
   the log, takes the session from a cookie or the Authorization field, opens and seals the
   bodies of the secure routes with AES-CBC, and runs each client command in a transaction that
   locks the player's row and keeps the answer, so that a command the client repeats gets the
   same answer without running twice. A handler that panics is answered with 500, its record
   says so, and the connection and the server go on. */

/* What every request shares: the database connections and the keys of the sessions and of the
   protocol. */
struct Server {
    std.pool::pool<std.postgres::connection> database;
    std.jwt::key_set keys;
    bytes cipher;
    std.log::logger log;
    std.log::panic_reports panics;
    atomic u64 requests;
};

/* The context of one request: its number, its logger, its account, its client command and the
   connection of the command's transaction. */
struct Call {
    u64 number;
    o<std.log::logger> log;
    std.string::string account;
    std.string::string command;
    o<std.pool::lease<std.postgres::connection>> transaction;
};

/* The panics of handlers that the application reported. */
atomic u32 panics = 0u32;

protected Call open_call(const Server* shared, const std.http::request* incoming) {
    incoming as void;
    u64 number = core::atomic_fetch_add(&shared->requests, 1u64, core::memory_order::relaxed) + 1u64;
    return Call {.number = number, .log = o::none, .account = std.string::create(), .command = std.string::create(),
                 .transaction = o::none};
}

/* A record of the logger of a request, or before the request has one of the server, with the
   place of the code that writes it. */
protected void note(const Server* shared, const Call* call, std.log::level value, str message, const std.log::fields* extra,
                    str caller) throws std.error::fault {
    switch (call->log) {
    case variant o::some(logger): logger->log_at(value, message, extra, caller);
    case variant o::none: shared->log.log_at(value, message, extra, caller);
    }
}

/* The record of a request whose handler panicked: the flow and its logger are gone, so the fields
   come from the notes of the request, which outlive it. */
protected void report_panic(const Server* shared, std.http::method sent, str target, const std.http::notes* noted,
                            const std.thread::panic_report* report) {
    constexpr str category = std.thread::panic_category(report);
    if (std.bytes::equal(category, "explicit") == true) {
        core::atomic_fetch_add(&panics, 1u32, core::memory_order::relaxed) as void;
    }
    try {
        std.log::fields record = std.log::fields::create();
        usize total = noted->count();
        for (usize index = 0usize; index < total; index += 1usize) {
            o<std.string::string> name = noted->name_at(index);
            o<std.string::string> value = noted->value_at(index);
            switch (move name) {
            case variant o::some(move named):
                switch (move value) {
                case variant o::some(move text): record.text(named, text);
                case variant o::none: break;
                }
            case variant o::none: drop value;
            }
        }
        record.text("http.method", std.http::method_name(sent));
        record.text("http.route", target);
        record.number("http.status_code", 500i64);
        str text = std.thread::panic_text(report);
        std.string::string panic_value = f"{category}: {text}";
        record.text("panic", panic_value);
        record.text("severity", "critical");
        shared->log.log_at(std.log::level::error, "panic recovered", &record, core::location());
    } catch (std.error::fault failure) {
        /* A record that cannot be made is lost; the request has its answer. */
        failure as void;
    }
}

/* A refusal as the clients read it: {"error":"..."}; the reasons are fixed words. */
protected std.http::response refusal(u16 status, str reason) throws std.alloc::alloc_error {
    std.string::string body = std.string::from_str("{\"error\":\"");
    body.append(reason);
    body.append("\"}");
    return std.http::response::json(status, body);
}

/* A JSON answer; `<`, `>` and `&` are escaped, so the text can stand inside a page of the admin
   panel. */
protected std.http::response answer(u16 status, std.string::string body) throws std.alloc::alloc_error {
    std.string::string escaped = std.json::escape_html(body);
    return std.http::response::json(status, escaped);
}

/* A JSON response of a value; the values of this server always encode. */
@generic<T: json_encode>
protected std.http::response encoded(u16 status, const T* body) throws std.alloc::alloc_error {
    try {
        return answer(status, std.json::marshal(body));
    } catch (std.json::error failure) {
        (move failure) as void;
    }
    return refusal(500u16, "encoding");
}

/* The identifier of a request: the X-Request-ID of the client, or a new UUID. */
protected std.string::string request_id(const std.http::request* incoming) throws std.alloc::alloc_error {
    switch (incoming->headers.get("X-Request-ID")) {
    case variant o::some(value):
        const u8[] given = *value;
        if (len(given) != 0usize) { return std.string::from_str(*value); }
    case variant o::none: break;
    }
    std.uuid::uuid fresh = std.uuid::v4();
    return f"{fresh}";
}

/* Every request gets its identifier, which its response names, its logger carries and its notes
   keep for the panic hook. */
protected async std.http::flow<Call> open_request(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    std.string::string id = request_id(&current.request);
    current.notes.set("request_id", id);
    std.log::fields bound = std.log::fields::create();
    bound.text("request_id", id);
    o<std.log::logger> old = core::replace(&current.context.log, o::some(shared->log.with(&bound)));
    drop old;
    try {
        current.response.headers.set("X-Request-ID", id);
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    return move current;
}

/* One record of every request when it is answered: its method, the pattern of its route and its
   status, at the level its status calls for. */
protected async std.http::flow<Call> close_request(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    std.string::string id = std.string::create();
    o<std.string::string> given = current.notes.get("request_id");
    switch (move given) {
    case variant o::some(move value): id.append(value);
    case variant o::none: break;
    }
    try {
        current.response.headers.set("X-Request-ID", id);
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    std.log::fields record = std.log::fields::create();
    record.text("http.method", std.http::method_name(current.request.method));
    if (std.string::len(&current.route) == 0usize) {
        record.text("http.route", current.request.path());
    } else {
        record.text("http.route", current.route);
    }
    u16 status = current.response.status;
    record.number("http.status_code", status as i64);
    std.log::level value = std.log::level::info;
    if (status >= 400u16) { value = std.log::level::warn; }
    if (status >= 500u16) { value = std.log::level::error; }
    note(&*shared, &current.context, value, "http request completed", &record, core::location());
    drop shared;
    return move current;
}

/* The session token of a request: the cookie of the game client, or a bearer token. */
protected o<std.string::string> token_of(const std.http::request* incoming) throws std.alloc::alloc_error {
    switch (incoming->cookie("arena_session")) {
    case variant o::some(value): return o::some(std.string::from_str(*value));
    case variant o::none: break;
    }
    switch (incoming->headers.get("Authorization")) {
    case variant o::some(value):
        if (std.text::starts_with(*value, "Bearer ") == true) {
            const u8[] text = *value;
            try {
                return o::some(std.string::from_utf8(text[7usize..len(text)]));
            } catch (std.string::string_error rejected) {
                rejected as void;
            }
        }
    case variant o::none: break;
    }
    return o::none;
}

/* The account of a valid session token, or 401 for everything under /api. */
protected async std.http::flow<Call> authenticate(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    o<std.string::string> token = token_of(&current.request);
    switch (move token) {
    case variant o::some(move text):
        try {
            std.jwt::validation rules = std.jwt::validation::create();
            rules.require_expiration = false;
            example.arena.sessions::Session session = std.jwt::verify_claims::<example.arena.sessions::Session>(
                &shared->keys, text, &rules, std.time::system_now());
            current.context.account.append(session.account);
            current.notes.set("usr.id", session.account);
            std.log::fields user = std.log::fields::create();
            user.text("usr.id", session.account);
            o<std.log::logger> child = o::none;
            switch (current.context.log) {
            case variant o::some(logger): child = o::some(logger->with(&user));
            case variant o::none: break;
            }
            switch (move child) {
            case variant o::some(move made):
                o<std.log::logger> old = core::replace(&current.context.log, o::some(move made));
                drop old;
            case variant o::none: break;
            }
            return move current;
        } catch (std.jwt::jwt_error rejected) {
            rejected as void;
        } catch (std.json::error rejected) {
            (move rejected) as void;
        } catch (std.crypto::crypto_error rejected) {
            rejected as void;
        } catch (std.time::time_error rejected) {
            rejected as void;
        }
    case variant o::none: break;
    }
    drop shared;
    return (move current).with(refusal(401u16, "unauthorized"));
}

/* The secure routes speak the protocol of the game client: a body is the initialization vector,
   then the AES-256-CBC ciphertext with PKCS#7 padding, both ways. */
protected bytes opened(const bytes* key, const bytes* body) throws std.crypto::crypto_error, std.alloc::alloc_error {
    const u8[] sealed = body->as_slice();
    throw (len(sealed) < 32usize) std.crypto::crypto_error {.code = std.crypto::error_code::invalid_length};
    return std.crypto::cbc_decrypt(key->as_slice(), sealed[0usize..16usize], sealed[16usize..len(sealed)]);
}

protected async std.http::flow<Call> open_body(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    try {
        bytes plain = opened(&shared->cipher, &current.request.body);
        bytes old = core::replace(&current.request.body, move plain);
        drop old;
        return move current;
    } catch (std.crypto::crypto_error rejected) {
        rejected as void;
    }
    return (move current).with(refusal(400u16, "sealed body"));
}

protected async std.http::flow<Call> seal_body(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    try {
        bytes sealed = std.crypto::random(16usize);
        bytes text = std.crypto::cbc_encrypt(shared->cipher.as_slice(), sealed.as_slice(),
                                             current.response.body.as_slice());
        std.bytes::append(&sealed, text.as_slice());
        bytes old = core::replace(&current.response.body, move sealed);
        drop old;
        current.response.headers.set("Content-Type", "application/octet-stream");
    } catch (std.crypto::crypto_error rejected) {
        rejected as void;
        return (move current).with(std.http::response::create(500u16));
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    return move current;
}

protected void add(array<std.postgres::value>* target, std.postgres::value item) throws std.alloc::alloc_error {
    try {
        target->push(move item);
    } catch (std.array::push_error<std.postgres::value> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* The answer kept for a client command. */
struct Kept { i64 status; std.string::string body; };

/* A client command runs in a transaction that locks the row of its player. A command that this
   player sent before gets the answer kept for it and does not run again. */
protected async std.http::flow<Call> begin_command(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    std.string::string command = std.string::create();
    switch (current.request.headers.get("X-Command-ID")) {
    case variant o::some(value): command.append(*value);
    case variant o::none: break;
    }
    current.context.command = move command;
    if (std.string::len(&current.context.command) == 0usize) {
        drop shared;
        return (move current).with(refusal(400u16, "command id"));
    }
    o<std.pool::lease<std.postgres::connection>> taken = shared->database.acquire();
    drop shared;
    switch (move taken) {
    case variant o::some(move lease):
        try {
            /* A command whose handler panicked gave its connection back inside the transaction. */
            if ((lease.get())->transaction_status() != std.postgres::transaction_status::idle) {
                await (lease.get())->rollback();
            }
            await (lease.get())->begin();
            array<std.postgres::value> locking = [];
            add(&locking, std.postgres::value::of_text(current.context.account));
            std.postgres::rows locked =
                await (lease.get())->query("SELECT id FROM users WHERE account_id = $1 FOR UPDATE", move locking);
            if (len(locked.items) != 1usize) {
                await (lease.get())->rollback();
                return (move current).with(refusal(404u16, "no player"));
            }
            array<std.postgres::value> asking = [];
            add(&asking, std.postgres::value::of_text(current.context.account));
            add(&asking, std.postgres::value::of_text(current.context.command));
            std.postgres::rows kept = await (lease.get())->query(
                "SELECT status, body FROM client_commands WHERE account_id = $1 AND command_id = $2", move asking);
            if (len(kept.items) == 1usize) {
                await (lease.get())->rollback();
                Kept again = kept.decode::<Kept>(0usize);
                std.http::response replayed = std.http::response::json(again.status as u16, again.body);
                replayed.headers.set("X-Replayed", "true");
                return (move current).with(move replayed);
            }
            current.context.transaction = o::some(move lease);
            return move current;
        } catch (std.postgres::pg_error failure) {
            (move failure) as void;
        } catch (std.http::http_error failure) {
            failure as void;
        }
        return (move current).with(refusal(500u16, "database"));
    case variant o::none: break;
    }
    return (move current).with(refusal(503u16, "busy"));
}

/* An answer with 5xx, or one whose statement failed (a nickname that is taken), rolls the command
   back; any other answer is kept with its changes. */
protected async std.http::flow<Call> finish_command(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    drop shared;
    o<std.pool::lease<std.postgres::connection>> held = core::replace(&current.context.transaction, o::none);
    switch (move held) {
    case variant o::some(move lease):
        try {
            if (current.response.status >= 500u16 ||
                (lease.get())->transaction_status() == std.postgres::transaction_status::failed) {
                await (lease.get())->rollback();
                return move current;
            }
            array<std.postgres::value> keeping = [];
            add(&keeping, std.postgres::value::of_text(current.context.account));
            add(&keeping, std.postgres::value::of_text(current.context.command));
            add(&keeping, std.postgres::value::integer(current.response.status as i64));
            add(&keeping, std.postgres::value::of_bytes(current.response.body.as_slice()));
            u64 stored = await (lease.get())->execute(
                "INSERT INTO client_commands (account_id, command_id, status, body) "
                "VALUES ($1, $2, $3, convert_from($4, 'UTF8'))",
                move keeping);
            stored as void;
            await (lease.get())->commit();
            return move current;
        } catch (std.postgres::pg_error failure) {
            (move failure) as void;
        }
        return (move current).with(refusal(500u16, "database"));
    case variant o::none: break;
    }
    return move current;
}

/* The connection of a command's transaction, which the commands middleware opened. */
protected const std.postgres::connection* transaction_of(const Call* call) {
    switch (call->transaction) {
    case variant o::some(lease): return lease->get();
    case variant o::none: break;
    }
    panic("a command outside its transaction");
}

struct Profile { std.string::string account; u64 request; };

protected async std.http::flow<Call> me(arc Server shared, std.http::flow<Call> current) throws std.error::fault {
    drop shared;
    Profile body = {.account = std.string::from_str(current.context.account),
                    .request = current.context.number};
    std.http::response result = encoded(200u16, &body);
    return (move current).with(move result);
}

/* The client keeps its session in a cookie: the token of the Authorization field is set as one. */
protected async std.http::flow<Call> keep_session(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    drop shared;
    o<std.string::string> token = token_of(&current.request);
    std.http::response kept = std.http::response::create(204u16);
    switch (move token) {
    case variant o::some(move text):
        std.http::cookie session = {.name = std.string::from_str("arena_session"), .value = move text,
                                     .path = o::some(std.string::from_str("/api")),
                                     .max_age = o::some(2592000i64), .secure = true, .http_only = true,
                                     .same_site = o::some(std.string::from_str("Strict"))};
        try {
            kept.set_cookie(&session);
        } catch (std.http::http_error rejected) {
            rejected as void;
        }
    case variant o::none: break;
    }
    return (move current).with(move kept);
}

struct Player { i64 id; std.string::string nickname; };

protected async std.http::flow<Call> player(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    o<std.pool::lease<std.postgres::connection>> taken = shared->database.acquire();
    drop shared;
    switch (move taken) {
    case variant o::some(move lease):
        array<std.postgres::value> asking = [];
        switch (current.request.param("id")) {
        case variant o::some(value): add(&asking, std.postgres::value::of_text(*value));
        case variant o::none: add(&asking, std.postgres::value::of_text(""));
        }
        try {
            if ((lease.get())->transaction_status() != std.postgres::transaction_status::idle) {
                await (lease.get())->rollback();
            }
            std.postgres::rows found =
                await (lease.get())->query("SELECT id, nickname FROM users WHERE id::text = $1", move asking);
            if (len(found.items) == 0usize) { return (move current).with(refusal(404u16, "no player")); }
            Player row = found.decode::<Player>(0usize);
            std.http::response result = encoded(200u16, &row);
            return (move current).with(move result);
        } catch (std.postgres::pg_error failure) {
            (move failure) as void;
        }
        return (move current).with(refusal(500u16, "database"));
    case variant o::none: break;
    }
    return (move current).with(refusal(503u16, "busy"));
}

struct Rename { std.string::string nick; };

/* The command that renames the player, in the transaction of its middleware. */
protected async std.http::flow<Call> rename(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    drop shared;
    try {
        Rename wanted = std.json::unmarshal(current.request.body.as_slice());
        array<std.postgres::value> changing = [];
        add(&changing, std.postgres::value::of_text(current.context.account));
        add(&changing, std.postgres::value::of_text(wanted.nick));
        u64 changed = await transaction_of(&current.context)->execute(
            "UPDATE users SET nickname = $2 WHERE account_id = $1", move changing);
        changed as void;
        std.http::response result = encoded(200u16, &wanted);
        return (move current).with(move result);
    } catch (std.json::error failure) {
        (move failure) as void;
        return (move current).with(refusal(400u16, "nick"));
    } catch (std.postgres::pg_error failure) {
        bool taken = failure.code == std.postgres::error_code::server &&
                     std.bytes::equal(failure.sqlstate.as_bytes(), "23505") == true;
        (move failure) as void;
        if (taken == true) { return (move current).with(refusal(409u16, "nick taken")); }
    }
    return (move current).with(refusal(500u16, "database"));
}

/* A command whose handler panics after it changed the row: the request is answered with 500,
   and the change goes with the transaction, which the next user of the connection rolls back. */
protected async std.http::flow<Call> crash(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    drop shared;
    try {
        array<std.postgres::value> changing = [];
        add(&changing, std.postgres::value::of_text(current.context.account));
        u64 changed = await transaction_of(&current.context)->execute(
            "UPDATE users SET nickname = 'Crashed' WHERE account_id = $1", move changing);
        changed as void;
    } catch (std.postgres::pg_error failure) {
        (move failure) as void;
        return (move current).with(refusal(500u16, "database"));
    }
    panic("the crash command");
}

struct Echo { std.string::string echo; std.string::string account; };

protected async std.http::flow<Call> echo(arc Server shared, std.http::flow<Call> current) throws std.error::fault {
    drop shared;
    try {
        Echo body = {.echo = std.string::from_utf8(current.request.body.as_slice()),
                     .account = std.string::from_str(current.context.account)};
        std.http::response result = encoded(200u16, &body);
        return (move current).with(move result);
    } catch (std.string::string_error failure) {
        failure as void;
    }
    return (move current).with(refusal(400u16, "text"));
}

/* Work that a request leaves running after its answer; this one fails with a panic, which nothing
   observes. */
protected async void late_failure(std.string::string account) {
    drop account;
    panic("a background reward failed");
}

/* A command that starts background work and answers at once. */
protected async std.http::flow<Call> background(arc Server shared, std.http::flow<Call> current)
    throws std.error::fault {
    drop shared;
    task<void> later = late_failure(std.string::from_str(current.context.account));
    (move later).detach();
    std.http::response queued = std.http::response::json(202u16, "{\"queued\":true}");
    return (move current).with(move queued);
}

/* The record of a panic that nothing observed, from background work. */
protected void log_panic(const Server* shared, std.log::panic_record record) throws std.error::fault {
    std.log::fields fields = std.log::fields::create();
    str category = record.category;
    str text = record.text;
    std.string::string panic_value = f"{category}: {text}";
    fields.text("panic", panic_value);
    fields.text("place", record.place);
    fields.text("severity", "critical");
    shared->log.log_at(std.log::level::error, "panic recovered", &fields, core::location());
}

/* The panics that nothing observed, as records of the log, until the server stops or a record
   cannot be written. */
protected async void drain_panics(arc Server shared) {
    try {
        while (true) {
            task_scope(1) waiting {
                std.log::panic_record record = await shared->panics.next();
                log_panic(&*shared, move record);
            }
        }
    } catch (std.error::fault failure) {
        failure as void;
    }
}

/* The application: the middleware in the order a request passes it, then the routes, each found
   by the most specific pattern whatever the order here. */
protected std.http::app<Server, Call> application() throws std.http::http_error, std.alloc::alloc_error {
    std.http::app<Server, Call> routes = std.http::app<Server, Call>::create(open_call);
    routes.around("/", open_request, close_request);
    routes.before("/api", authenticate);
    routes.around("/api/secure", open_body, seal_body);
    routes.around("/api/commands", begin_command, finish_command);
    routes.route(std.http::method::get, "/api/me", me);
    routes.route(std.http::method::post, "/api/session", keep_session);
    routes.route(std.http::method::get, "/api/users/{id}", player);
    routes.route(std.http::method::get, "/api/users/me", me);
    routes.route(std.http::method::post, "/api/commands/nick", rename);
    routes.route(std.http::method::post, "/api/commands/crash", crash);
    routes.route(std.http::method::post, "/api/secure/echo", echo);
    routes.route(std.http::method::post, "/api/background", background);
    std.http::cors_policy policy = {.credentials = true};
    try {
        policy.origins.push(std.string::from_str("https://game.example"));
    } catch (std.array::push_error<std.string::string> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
    routes.cors(move policy);
    routes.redirect_trailing_slash(true);
    routes.method_not_allowed(false);
    routes.on_panic(report_panic);
    return move routes;
}

/* The patterns of the routes are fixed, so a refusal of one is a defect of this module. */
protected std.http::app<Server, Call> built() throws std.alloc::alloc_error {
    try {
        return application();
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
    panic("a pattern of the routes is refused");
}

protected void add_connection(array<std.postgres::connection>* target, std.postgres::connection item)
    throws std.alloc::alloc_error {
    try {
        target->push(move item);
    } catch (std.array::push_error<std.postgres::connection> rejected) {
        (move rejected) as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* The log of the server: one JSON object per line on standard error, level first, then the
   fields, the time in RFC 3339 with the digits the clock gives, and the message. */
protected std.log::logger server_log(const std.log::writer* journal) throws std.alloc::alloc_error {
    std.log::logger base = journal->logger(std.log::level::info, std.log::format::json);
    std.log::layout shape = std.log::layout::standard();
    shape.time_name = std.string::from_str("timestamp");
    shape.task_name = o::none;
    shape.time_digits = 9u32;
    shape.trim_time = true;
    shape.omit_empty_message = true;
    shape.caller_name = o::some(std.string::from_str("caller"));
    shape.sequence = std.log::order::level_first;
    base.set_layout(move shape);
    std.log::fields statics = std.log::fields::create();
    statics.text("service", "arena");
    statics.text("component", "http");
    return base.with(&statics);
}

/* server KEY CIPHER: the application on a loopback port with two database connections, until
   SIGTERM or SIGINT; then what it served. Its log goes to standard error until the last logger,
   that of the server state, is gone. */
async std.string::string run(std.jwt::key_set keys, bytes cipher)
    throws std.postgres::pg_error, std.error::fault {
    array<std.postgres::connection> connections = [];
    for (u32 index = 0u32; index < 2u32; index += 1u32) {
        std.postgres::connection made = await std.postgres::connect(std.postgres::options::from_environment());
        add_connection(&connections, move made);
    }
    std.log::writer journal = std.log::writer::create(1024usize);
    arc Server shared = new arc Server {.database = std.pool::pool<std.postgres::connection>::create(move connections),
                                        .keys = move keys, .cipher = move cipher, .log = server_log(&journal),
                                        .panics = std.log::panic_reports::listen(16usize), .requests = 0u64};
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16, .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 16u32, .reuse_address = true, .v6_only = false};
    std.net::tcp_listener listener = await local.listen(options);
    std.net::socket_address bound = listener.local_address();
    u16 port = bound.port;
    std.sync::channel<std.service::stop> channel = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&channel);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move channel);
    array<std.service::listener> listeners = [];
    try {
        listeners.push(std.service::listener::tcp(move listener));
    } catch (std.array::push_error<std.service::listener> rejected) {
        drop rejected;
        throw std.alloc::alloc_error::out_of_memory;
    }
    std.service::options settings = {.capacity = 16u32, .stop_on_signals = true};
    std.http::limits bounds = {};
    task<void> draining = drain_panics(std.arc::clone(&shared));
    await std.console::println(f"ready {port}");
    std.service::report account = await std.http::serve_app(move listeners, settings, move stop,
                                                            std.service::health::create(),
                                                            std.arc::clone(&shared), built(), bounds);
    (move draining).cancel();
    drop stopper;
    bool sweeping = true;
    while (sweeping == true) {
        o<std.log::panic_record> left = shared->panics.take();
        switch (move left) {
        case variant o::some(move record): log_panic(&*shared, move record);
        case variant o::none: sweeping = false;
        }
    }
    u64 accepted = account.accepted;
    u64 failed = account.failed;
    u64 requests = core::atomic_load(&shared->requests, core::memory_order::relaxed);
    u32 reported = core::atomic_load(&panics, core::memory_order::relaxed);
    usize free = shared->database.available();
    drop shared;
    u64 lines = await std.log::writer::to_stderr(move journal);
    return f"stopped: accepted {accepted}, failed {failed}, requests {requests}, panics {reported}, free connections {free}, log lines {lines}\n";
}
