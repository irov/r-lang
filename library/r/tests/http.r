module tests.std.http;
import std.test;
import std.bufio;
import std.http;
import std.service;
import std.tls;
import std.deflate;
import std.text;

// The tests of std.http (Library R-SLIB-HTTP-0001..0011): header sections, the router with path
// parameters, wildcards and hooks, the server over loopback TCP with keep-alive, HEAD, chunked
// request bodies and the answers to requests it cannot read, the client, streamed responses and
// event streams, and upgrade routes with the upgrade of the client. Run in test mode
// (Core R-FUNC-0025).

struct Counter { atomic u32 hits; };

protected async std.http::response hello(arc Counter state, std.http::request incoming)
    throws std.error::fault {
    const Counter* shared = &*state;
    u32 before = core::atomic_fetch_add(&shared->hits, 1u32, core::memory_order::relaxed);
    before as void;
    drop incoming;
    return std.http::response::text(200u16, "hello");
}

protected async std.http::response item(arc Counter state, std.http::request incoming)
    throws std.error::fault {
    drop state;
    std.string::string text = std.string::from_str("item ");
    switch (incoming.param("id")) {
    case variant o::some(id): std.string::append_str(&text, *id);
    case variant o::none: std.string::append_str(&text, "?");
    }
    return std.http::response::text(200u16, text);
}

protected async std.http::response file(arc Counter state, std.http::request incoming)
    throws std.error::fault {
    drop state;
    std.string::string text = std.string::from_str("file ");
    switch (incoming.param("*")) {
    case variant o::some(rest): std.string::append_str(&text, *rest);
    case variant o::none: break;
    }
    return std.http::response::text(200u16, text);
}

protected async std.http::response echo(arc Counter state, std.http::request incoming)
    throws std.error::fault {
    drop state;
    std.http::response result = std.http::response::create(201u16);
    bytes empty = {};
    result.body = core::replace(&incoming.body, move empty);
    return move result;
}

protected async std.http::response broken(arc Counter state, std.http::request incoming)
    throws std.error::fault {
    drop state;
    drop incoming;
    u16 port = std.convert::parse_u16("not a number", 10u32);
    port as void;
    return std.http::response::create(200u16);
}

protected o<std.http::response> deny(const Counter* state, const std.http::request* incoming)
    throws std.alloc::alloc_error {
    state as void;
    if (incoming->headers.contains("X-Deny") == true) {
        return o::some(std.http::response::text(403u16, "denied"));
    }
    return o::none;
}

protected void mark(std.http::method sent, str target, std.http::response* result)
    throws std.alloc::alloc_error {
    sent as void;
    target as void;
    try {
        result->headers.add("X-Served", "yes");
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
}

protected void add_field(std.http::response* result, str name, str value) throws std.alloc::alloc_error {
    try {
        result->headers.add(name, value);
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
}

protected async std.http::response moved(arc Counter state, std.http::request incoming)
    throws std.error::fault {
    drop state;
    drop incoming;
    std.http::response result = std.http::response::create(302u16);
    add_field(&result, "Location", "/hello");
    return move result;
}

protected async std.http::response submit(arc Counter state, std.http::request incoming)
    throws std.error::fault {
    drop state;
    drop incoming;
    std.http::response result = std.http::response::create(303u16);
    add_field(&result, "Location", "result?from=submit");
    return move result;
}

protected async std.http::response result_page(arc Counter state, std.http::request incoming)
    throws std.error::fault {
    drop state;
    std.string::string text = std.string::from_str(std.http::method_name(incoming.method));
    std.string::append_str(&text, " ");
    std.string::append_str(&text, incoming.target);
    std.string::string length = std.string::create();
    usize size = len(incoming.body);
    std.string::string shown = f" body={size}";
    std.string::append_str(&text, shown);
    drop length;
    return std.http::response::text(200u16, text);
}

protected async std.http::response forever(arc Counter state, std.http::request incoming)
    throws std.error::fault {
    drop state;
    drop incoming;
    std.http::response result = std.http::response::create(307u16);
    add_field(&result, "Location", "/loop");
    return move result;
}

protected async std.http::response zipped(arc Counter state, std.http::request incoming)
    throws std.error::fault {
    drop state;
    drop incoming;
    std.http::response result = std.http::response::create(200u16);
    add_field(&result, "Content-Encoding", "gzip");
    try {
        bytes packed = std.deflate::deflate("compressed text, compressed text, compressed text",
                                            std.deflate::format::rfc1952, 6u8);
        result.body = move packed;
    } catch (std.deflate::error rejected) {
        rejected as void;
        return std.http::response::create(500u16);
    }
    return move result;
}

/* Streams three events. */
protected async void events(arc Counter state, std.http::request incoming, std.http::body_writer writer)
    throws std.error::fault {
    drop state;
    drop incoming;
    std.http::response head = std.http::response::create(200u16);
    add_field(&head, "Content-Type", "text/event-stream");
    task_scope(1) begin {
        bool open = await writer.start(move head);
        if (open == false) { return; }
    }
    for (u32 index = 0u32; index < 3u32; index += 1u32) {
        std.string::string id = f"{index}";
        std.string::string data = f"n={index}\nsecond line";
        std.string::string text = std.http::sse_event("tick", id, data);
        task_scope(1) io {
            bool sent = await writer.send_text(text);
            if (sent == false) { return; }
        }
    }
}

/* Starts a stream and fails before its end. */
protected async void broken_stream(arc Counter state, std.http::request incoming,
                                   std.http::body_writer writer) throws std.error::fault {
    drop state;
    drop incoming;
    task_scope(1) io {
        bool sent = await writer.send_text("partial");
        sent as void;
    }
    u16 port = std.convert::parse_u16("not a number", 10u32);
    port as void;
}

/* Ends without a head. */
protected async void silent_stream(arc Counter state, std.http::request incoming,
                                   std.http::body_writer writer) throws std.error::fault {
    drop state;
    drop incoming;
    drop writer;
}

/* Sends its head, then waits much longer than any test; only a peer that leaves ends it early
   (R-SLIB-HTTP-0009). */
protected async void hang(arc Counter state, std.http::request incoming, std.http::body_writer writer)
    throws std.error::fault {
    drop state;
    drop incoming;
    task_scope(1) begin {
        bool open = await writer.start(std.http::response::create(200u16));
        if (open == false) { return; }
    }
    await std.time::sleep_for(std.time::duration_from_seconds(60i64));
}

/* The complete messages the parser holds, each checked for its event type. */
protected u32 count_messages(std.http::sse_parser* parser)
    throws std.test::failure, std.http::http_error, std.alloc::alloc_error {
    u32 count = 0u32;
    while (true) {
        o<std.http::sse_message> got = parser->next();
        bool found = false;
        switch (move got) {
        case variant o::some(move message):
            std.test::equal_text(message.event, "tick");
            count += 1u32;
            found = true;
        case variant o::none: break;
        }
        if (found == false) { return count; }
    }
    return count;
}

/* Whether the parser holds no complete message. */
protected bool drained(std.http::sse_parser* parser) throws std.http::http_error, std.alloc::alloc_error {
    switch (parser->next()) {
    case variant o::some(message):
        message as void;
        return false;
    case variant o::none: return true;
    }
}

protected std.http::router<Counter> routes() throws std.http::http_error, std.alloc::alloc_error {
    std.http::router<Counter> result = std.http::router<Counter>::create();
    result.add(std.http::method::get, "/hello", hello);
    result.add(std.http::method::get, "/items/{id}", item);
    result.add(std.http::method::delete, "/items/{id}", item);
    result.add(std.http::method::get, "/files/*", file);
    result.add(std.http::method::post, "/echo", echo);
    result.add(std.http::method::get, "/broken", broken);
    result.add(std.http::method::get, "/moved", moved);
    result.add(std.http::method::post, "/submit", submit);
    result.add(std.http::method::get, "/result", result_page);
    result.add(std.http::method::get, "/loop", forever);
    result.add(std.http::method::get, "/zipped", zipped);
    result.add_stream(std.http::method::get, "/events", events);
    result.add_stream(std.http::method::get, "/broken-stream", broken_stream);
    result.add_stream(std.http::method::get, "/silent-stream", silent_stream);
    result.add_stream(std.http::method::get, "/hang", hang);
    result.before(deny);
    result.after(mark);
    return move result;
}

protected async std.net::tcp_listener open_listener() throws std.error::fault {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 16u32, .reuse_address = true, .v6_only = false};
    return await local.listen(options);
}

/* Sends a request on the connection and reads its response. */
@scoped
protected async std.http::response call(std.bufio::reader<std.net::tcp_stream>* connection,
                                        std.http::request message)
    throws std.error::fault, std.http::http_error {
    std.http::limits bounds = {};
    task_scope(1) io {
        await std.http::write_request(&connection->source, &message, "localhost");
        return await std.http::read_response(connection, message.method, &bounds);
    }
}

protected std.http::request get(str target) throws std.alloc::alloc_error {
    return std.http::request::create(std.http::method::get, target);
}

protected void expect_text(const std.http::response* result, u16 status, str body)
    throws std.test::failure, std.alloc::alloc_error {
    std.test::equal(result->status, status);
    std.test::check(std.bytes::equal(result->body.as_slice(), body), body);
}

/* The client side of the server test: requests on one kept-alive connection, then a drain. */
protected async u32 clients(std.net::socket_address endpoint,
                            std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.test::failure, std.http::http_error {
    std.net::tcp_stream stream = await endpoint.connect();
    std.bufio::reader<std.net::tcp_stream> connection =
        std.bufio::reader<std.net::tcp_stream>::create(move stream, 4096usize);
    u32 checked = 0u32;
    task_scope(1) io {
        std.http::response first = await call(&connection, get("/hello"));
        expect_text(&first, 200u16, "hello");
        switch (first.headers.get("X-Served")) {
        case variant o::some(value): std.test::equal_text(*value, "yes");
        case variant o::none: std.test::fail("the after hook");
        }
        std.test::check(first.headers.contains("Date"), "a Date field");
        std.http::response second = await call(&connection, get("/items/a%20b"));
        expect_text(&second, 200u16, "item a b");
        std.http::response third = await call(&connection, get("/files/css/site.css?v=1"));
        expect_text(&third, 200u16, "file css/site.css");
        std.http::response missing = await call(&connection, get("/nothing"));
        expect_text(&missing, 404u16, "Not Found");
        std.http::request wrong = std.http::request::create(std.http::method::put, "/items/7");
        std.http::response refused = await call(&connection, move wrong);
        std.test::equal(refused.status, 405u16);
        switch (refused.headers.get("Allow")) {
        case variant o::some(value): std.test::equal_text(*value, "GET, DELETE");
        case variant o::none: std.test::fail("Allow");
        }
        std.http::request posted = std.http::request::create(std.http::method::post, "/echo");
        std.bytes::append(&posted.body, "payload");
        std.http::response echoed = await call(&connection, move posted);
        expect_text(&echoed, 201u16, "payload");
        std.http::request head = std.http::request::create(std.http::method::head, "/hello");
        std.http::response headed = await call(&connection, move head);
        std.test::equal(headed.status, 200u16);
        std.test::equal(len(headed.body), 0usize);
        switch (headed.headers.get("Content-Length")) {
        case variant o::some(value): std.test::equal_text(*value, "5");
        case variant o::none: std.test::fail("Content-Length of HEAD");
        }
        std.http::request denied = get("/hello");
        denied.headers.add("X-Deny", "1");
        std.http::response forbidden = await call(&connection, move denied);
        expect_text(&forbidden, 403u16, "denied");
        std.http::response failed = await call(&connection, get("/broken"));
        std.test::equal(failed.status, 500u16);
        std.http::request last = get("/hello");
        last.headers.add("Connection", "close");
        std.http::response closing = await call(&connection, move last);
        std.test::check(closing.headers.has_token("Connection", "close"), "Connection: close");
        checked += 1u32;
    }
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return checked;
}

@test
async void serves_routes_on_a_kept_alive_connection()
    throws std.error::fault, std.test::failure, std.http::http_error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Counter state = new arc Counter {.hits = 0u32};
    std.service::options settings = {};
    std.http::limits bounds = {};
    task_scope(2) group {
        auto server = std.http::serve(move listener, settings, move stop, std.arc::clone(&state),
                                      routes(), bounds);
        auto client = clients(endpoint, move stopper);
        u32 checked = await move client;
        std.service::report account = await move server;
        std.test::equal(checked, 1u32);
        std.test::equal(account.accepted, 1u64);
        std.test::equal(account.failed, 0u64);
    }
    const Counter* view = &*state;
    std.test::equal(core::atomic_load(&view->hits, core::memory_order::relaxed), 3u32);
}

const constexpr str AUTHORITY =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBkDCCATWgAwIBAgIUapLhhLDwmYV1Rwi43Y/ZhIFkSpowCgYIKoZIzj0EAwIw\n"
    "FDESMBAGA1UEAwwJUiBUZXN0IENBMCAXDTI1MDEwMTAwMDAwMFoYDzIxMjUwMTAx\n"
    "MDAwMDAwWjAUMRIwEAYDVQQDDAlSIFRlc3QgQ0EwWTATBgcqhkjOPQIBBggqhkjO\n"
    "PQMBBwNCAAQ+S5UBiZd/pzQwPA5hAeohScE19KAxopyXfRlmCO1EclFKdu2sdR16\n"
    "Z8hd/B6JLqRxYsFdWodr4YUazHg8gxZbo2MwYTAdBgNVHQ4EFgQUkOaW7C/+j62Z\n"
    "A7b23fhYw5e2NzgwHwYDVR0jBBgwFoAUkOaW7C/+j62ZA7b23fhYw5e2NzgwDwYD\n"
    "VR0TAQH/BAUwAwEB/zAOBgNVHQ8BAf8EBAMCAQYwCgYIKoZIzj0EAwIDSQAwRgIh\n"
    "AJlGe7nQxNIqJVDH0gJijaOgt00j5Mi4vZQFDyJJntnGAiEA9NJSvEBqM8BlDipd\n"
    "vvbNilGBCu+SIb2DldIkJkkFMNA=\n"
    "-----END CERTIFICATE-----\n";

const constexpr str SERVER_CERTIFICATE =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIBqTCCAU+gAwIBAgIBAjAKBggqhkjOPQQDAjAUMRIwEAYDVQQDDAlSIFRlc3Qg\n"
    "Q0EwIBcNMjUwMTAxMDAwMDAwWhgPMjEyNTAxMDEwMDAwMDBaMBQxEjAQBgNVBAMM\n"
    "CWxvY2FsaG9zdDBZMBMGByqGSM49AgEGCCqGSM49AwEHA0IABJj7DUw+i1Lb7ymP\n"
    "eHpocSyk2KyNbnMi40Jjr2/2EbuVioKl+/8R6XXzjurdLrB8ecySjj3WhhAXIW2R\n"
    "gIOFT9ejgY8wgYwwCQYDVR0TBAIwADAOBgNVHQ8BAf8EBAMCB4AwEwYDVR0lBAww\n"
    "CgYIKwYBBQUHAwEwGgYDVR0RBBMwEYIJbG9jYWxob3N0hwR/AAABMB0GA1UdDgQW\n"
    "BBR0kVv3v5fYCoPhs/hbu3JWWIYYHTAfBgNVHSMEGDAWgBSQ5pbsL/6PrZkDtvbd\n"
    "+FjDl7Y3ODAKBggqhkjOPQQDAgNIADBFAiBnEmKpaZ784hn4T42fsKtgE4vE5nt3\n"
    "7Kg/TrcMfiVMfAIhAKbPXo+gxuc0rJWbA1pTX3OFcZjjd6m0e5FNLZn8gYNA\n"
    "-----END CERTIFICATE-----\n";

const constexpr str SERVER_KEY =
    "-----BEGIN EC PRIVATE KEY-----\n"
    "MHcCAQEEIMz6l1l6INXhtQIwqD+ZIuvL6SuWr+L9f8QfDoMb6LROoAoGCCqGSM49\n"
    "AwEHoUQDQgAEmPsNTD6LUtvvKY94emhxLKTYrI1ucyLjQmOvb/YRu5WKgqX7/xHp\n"
    "dfOO6t0usHx5zJKOPdaGEBchbZGAg4VP1w==\n"
    "-----END EC PRIVATE KEY-----\n";

protected std.string::string base_of(std.net::socket_address endpoint, str scheme)
    throws std.alloc::alloc_error {
    u16 port = endpoint.port;
    return f"{scheme}://localhost:{port}";
}

protected std.string::string url_at(const std.string::string* base, str path)
    throws std.alloc::alloc_error {
    std.string::string text = std.string::from_str(*base);
    std.string::append_str(&text, path);
    return move text;
}

/* The client side of the client test: requests through one client, then a drain. */
/* Plain requests: keep-alive, a redirect, a 303 after POST and a gzip body. */
@scoped
protected async void plain_requests(std.http::client* owner, const std.string::string* base)
    throws std.error::fault, std.test::failure, std.http::http_error, std.tls::tls_error {
    std.string::string hello_url = url_at(base, "/hello");
    std.string::string moved_url = url_at(base, "/moved");
    std.string::string submit_url = url_at(base, "/submit");
    std.string::string zipped_url = url_at(base, "/zipped");
    task_scope(1) io {
        std.http::response first = await owner->get(hello_url);
        expect_text(&first, 200u16, "hello");
        std.test::equal(owner->idle_count(), 1usize);
        std.http::response again = await owner->get(hello_url);
        expect_text(&again, 200u16, "hello");
        std.test::equal(owner->idle_count(), 1usize);
        std.http::response followed = await owner->get(moved_url);
        expect_text(&followed, 200u16, "hello");
        std.http::request form = std.http::request::create(std.http::method::post, "/");
        std.bytes::append(&form.body, "name=value");
        form.headers.add("Content-Type", "application/x-www-form-urlencoded");
        std.http::response seen = await owner->send(move form, submit_url);
        expect_text(&seen, 200u16, "GET /result?from=submit body=0");
        std.http::response inflated = await owner->get(zipped_url);
        expect_text(&inflated, 200u16, "compressed text, compressed text, compressed text");
        std.test::check(inflated.headers.contains("Content-Encoding") == false, "decoded");
    }
}

/* A streamed event response, its HEAD form and a stream without a head. */
@scoped
protected async void streamed_requests(std.http::client* owner, const std.string::string* base)
    throws std.error::fault, std.test::failure, std.http::http_error, std.tls::tls_error {
    std.string::string events_url = url_at(base, "/events");
    std.string::string silent_url = url_at(base, "/silent-stream");
    task_scope(1) io {
        std.http::response streamed = await owner->get(events_url);
        std.test::equal(streamed.status, 200u16);
        std.test::check(streamed.headers.has_token("Transfer-Encoding", "chunked"), "chunked");
        std.http::sse_parser parser = std.http::sse_parser::create();
        const u8[] stream = streamed.body.as_slice();
        usize half = len(stream) / 2usize;
        parser.feed(stream[0usize..half]);
        u32 received = 0u32;
        u32 early = count_messages(&parser);
        parser.feed(stream[half..len(stream)]);
        received += early;
        while (true) {
            o<std.http::sse_message> got = parser.next();
            bool found = false;
            switch (move got) {
            case variant o::some(move message):
                std.string::string expected = f"n={received}\nsecond line";
                std.test::equal_text(message.data, expected);
                std.string::string id = f"{received}";
                std.test::equal_text(message.id, id);
                received += 1u32;
                found = true;
            case variant o::none: break;
            }
            if (found == false) { break; }
        }
        std.test::equal(received, 3u32);
        std.http::request head_events = std.http::request::create(std.http::method::head, "/");
        std.http::response headed = await owner->send(move head_events, events_url);
        std.test::equal(headed.status, 200u16);
        std.test::equal(len(headed.body), 0usize);
        std.http::response silent = await owner->get(silent_url);
        std.test::equal(silent.status, 500u16);
    }
}

protected bool present(o<bytes> value) {
    switch (value) {
    case variant o::some(data):
        data as void;
        return true;
    case variant o::none: return false;
    }
}

/* Opens a request and returns the stream of its response. */
@scoped
protected async o<std.http::streamed> open_stream(const std.http::client* owner, str address)
    throws std.error::fault, std.http::http_error, std.tls::tls_error {
    o<std.http::streamed> opened = o::none;
    task_scope(1) io {
        std.http::streamed got = await owner->open(std.http::request::create(std.http::method::get, "/"), address);
        o<std.http::streamed> old = core::replace(&opened, o::some(move got));
        drop old;
    }
    return move opened;
}

/* The events of a stream read piece by piece as they arrive, and whether the end stays. */
@scoped
protected async u32 read_events(std.http::streamed* opened)
    throws std.error::fault, std.test::failure, std.http::http_error {
    std.http::sse_parser parser = std.http::sse_parser::create();
    bool more = true;
    while (more == true) {
        o<bytes> piece = o::none;
        task_scope(1) io {
            o<bytes> got = await opened->next();
            o<bytes> old = core::replace(&piece, move got);
            drop old;
        }
        switch (move piece) {
        case variant o::some(move data): parser.feed(data.as_slice());
        case variant o::none: more = false;
        }
    }
    o<bytes> after = o::none;
    task_scope(1) io {
        o<bytes> got = await opened->next();
        o<bytes> old = core::replace(&after, move got);
        drop old;
    }
    std.test::check(present(move after) == false, "nothing after the end");
    return count_messages(&parser);
}

/* R-SLIB-HTTP-0012: the event stream read piece by piece as it arrives, and a stream that the
   client leaves after its head, whose handler the server then cancels. */
@scoped
protected async void opened_requests(const std.http::client* owner, const std.string::string* base)
    throws std.error::fault, std.test::failure, std.http::http_error, std.tls::tls_error {
    std.string::string events_url = url_at(base, "/events");
    std.string::string hang_url = url_at(base, "/hang");
    o<std.http::streamed> events_stream = o::none;
    task_scope(1) io {
        o<std.http::streamed> got = await open_stream(owner, events_url);
        o<std.http::streamed> old = core::replace(&events_stream, move got);
        drop old;
    }
    switch (move events_stream) {
    case variant o::some(move opened):
        std.test::equal(opened.head.status, 200u16);
        std.test::equal(len(opened.head.body), 0usize);
        task_scope(1) io {
            u32 count = await read_events(&opened);
            std.test::equal(count, 3u32);
        }
    case variant o::none: std.test::fail("an event stream");
    }
    task_scope(1) io {
        o<std.http::streamed> left = await open_stream(owner, hang_url);
        switch (move left) {
        case variant o::some(move stream):
            std.test::equal(stream.head.status, 200u16);
            drop stream;
        case variant o::none: std.test::fail("a stream that hangs");
        }
    }
}


protected async u32 client_requests(std.string::string base, std.http::client owner,
                                    std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.test::failure, std.http::http_error, std.tls::tls_error {
    std.http::client web = move owner;
    std.string::string hello_url = url_at(&base, "/hello");
    std.string::string loop_url = url_at(&base, "/loop");
    std.string::string broken_url = url_at(&base, "/broken-stream");
    u32 checked = 0u32;
    task_scope(1) io {
        await plain_requests(&web, &base);
        await streamed_requests(&web, &base);
        await opened_requests(&web, &base);
        checked += 1u32;
    }
    try {
        task_scope(1) io {
            std.http::response partial = await web.get(broken_url);
            drop partial;
        }
        std.test::fail("a stream whose handler failed");
    } catch (std.http::http_error rejected) {
        std.test::check(rejected.code == std.http::error_code::unexpected_end, "unexpected_end");
    }
    task_scope(1) after {
        std.http::response again_hello = await web.get(hello_url);
        expect_text(&again_hello, 200u16, "hello");
    }
    try {
        task_scope(1) io {
            std.http::response looping = await web.get(loop_url);
            drop looping;
        }
        std.test::fail("redirects without end");
    } catch (std.http::http_error rejected) {
        std.test::check(rejected.code == std.http::error_code::too_many_redirects, "too_many_redirects");
        checked += 1u32;
    }
    try {
        task_scope(1) io {
            std.http::response other = await web.get("ftp://localhost/file");
            drop other;
        }
        std.test::fail("an ftp URL");
    } catch (std.http::http_error rejected) {
        std.test::check(rejected.code == std.http::error_code::unsupported_scheme, "unsupported_scheme");
        checked += 1u32;
    }
    try {
        task_scope(1) io {
            std.http::response other = await web.get("not a url");
            drop other;
        }
        std.test::fail("an invalid URL");
    } catch (std.http::http_error rejected) {
        std.test::check(rejected.code == std.http::error_code::invalid_url, "invalid_url");
        checked += 1u32;
    }
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return checked;
}

@test
async void the_client_follows_redirects_and_reuses_connections()
    throws std.error::fault, std.test::failure, std.http::http_error, std.tls::tls_error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Counter state = new arc Counter {.hits = 0u32};
    std.service::options settings = {};
    std.http::limits bounds = {};
    std.http::client_options client_settings = {};
    task_scope(2) group {
        auto server = std.http::serve(move listener, settings, move stop, move state, routes(),
                                      bounds);
        auto client = client_requests(base_of(endpoint, "http"),
                                      std.http::client::create(client_settings), move stopper);
        u32 checked = await move client;
        std.service::report account = await move server;
        std.test::equal(checked, 4u32);
        std.test::equal(account.failed, 0u64);
        std.test::equal(account.cancelled, 0u64);
    }
}

protected async u32 secure_requests(std.string::string base, std.http::client owner,
                                    std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.test::failure, std.http::http_error, std.tls::tls_error {
    std.http::client web = move owner;
    std.string::string hello_url = url_at(&base, "/hello");
    std.string::string items_url = url_at(&base, "/items/42");
    u32 checked = 0u32;
    task_scope(1) io {
        std.http::response first = await web.get(hello_url);
        expect_text(&first, 200u16, "hello");
        std.http::response second = await web.get(items_url);
        expect_text(&second, 200u16, "item 42");
        std.test::equal(web.idle_count(), 1usize);
        checked += 1u32;
    }
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return checked;
}

@test
async void serves_and_requests_https()
    throws std.error::fault, std.test::failure, std.http::http_error, std.tls::tls_error {
    std.tls::config server_tls = std.tls::server_config();
    std.string::string key_text = std.string::from_str(SERVER_KEY);
    std.secret::buffer key = std.secret::from_bytes((move key_text).into_bytes());
    server_tls.set_identity(SERVER_CERTIFICATE, &key);
    std.tls::config client_tls = std.tls::client_config();
    client_tls.add_authority(AUTHORITY);
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Counter state = new arc Counter {.hits = 0u32};
    std.service::options settings = {};
    std.http::limits bounds = {};
    std.http::client_options client_settings = {};
    arc std.tls::config shared_server = new arc std.tls::config(move server_tls);
    arc std.tls::config shared_client = new arc std.tls::config(move client_tls);
    task_scope(2) group {
        auto server = std.http::serve_tls(move listener, settings, move stop, move state, routes(),
                                          bounds, move shared_server);
        auto client = secure_requests(base_of(endpoint, "https"),
                                      std.http::client::with_tls(client_settings, move shared_client),
                                      move stopper);
        u32 checked = await move client;
        std.service::report account = await move server;
        std.test::equal(checked, 1u32);
        std.test::equal(account.failed, 0u64);
    }
}

/* Sends raw bytes on a new connection, ends its write direction and returns the whole reply. */
@scoped
protected async std.string::string raw_exchange(std.net::socket_address endpoint, str text)
    throws std.error::fault {
    std.string::string message = std.string::from_str(text);
    std.net::tcp_stream client = await endpoint.connect();
    bytes reply = {};
    u8[256] chunk = {};
    task_scope(1) io {
        await std.net::tcp_write_all_from(&client, message);
        await std.net::tcp_shutdown(&client, std.net::shutdown_direction::write);
        while (true) {
            usize count = await std.net::tcp_read_into(&client, &chunk);
            if (count == 0usize) { break; }
            reply.append(chunk[0usize..count]);
        }
    }
    return std.string::from_utf8(reply.as_slice());
}

protected void expect_prefix(const std.string::string* reply, str prefix)
    throws std.test::failure, std.alloc::alloc_error {
    std.test::check(std.text::starts_with(*reply, prefix), *reply);
}

protected async u32 protocol_clients(std.net::socket_address endpoint,
                                     std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.test::failure {
    u32 checked = 0u32;
    task_scope(1) io {
        std.string::string bad = await raw_exchange(endpoint, "GARBAGE\r\n\r\n");
        expect_prefix(&bad, "HTTP/1.1 400 Bad Request\r\n");
        std.test::check(std.text::contains(bad, "Connection: close\r\n"), "closed");
        std.string::string chunked = await raw_exchange(endpoint,
            "POST /echo HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\nConnection: close\r\n\r\n"
            "3;note=1\r\nabc\r\n2\r\nde\r\n0\r\nTrailer: t\r\n\r\n");
        expect_prefix(&chunked, "HTTP/1.1 201 Created\r\n");
        std.test::check(std.text::ends_with(chunked, "\r\n\r\nabcde"), chunked);
        std.string::string large = await raw_exchange(endpoint,
            "GET /hello HTTP/1.1\r\nHost: x\r\nX-Long: 0123456789012345678901234567890123456789"
            "0123456789012345678901234567890123456789012345678901234567890123456789012345678901"
            "2345678901234567890123456789012345678901234567890123456789012345678901234567890123"
            "4567890123456789012345678901234567890123456789\r\n\r\n");
        expect_prefix(&large, "HTTP/1.1 431 Request Header Fields Too Large\r\n");
        std.string::string body = await raw_exchange(endpoint,
            "POST /echo HTTP/1.1\r\nHost: x\r\nContent-Length: 17\r\n\r\n01234567890123456");
        expect_prefix(&body, "HTTP/1.1 413 Content Too Large\r\n");
        std.string::string old = await raw_exchange(endpoint, "GET /hello HTTP/1.0\r\n\r\n");
        expect_prefix(&old, "HTTP/1.1 200 OK\r\n");
        std.test::check(std.text::ends_with(old, "hello"), "HTTP/1.0 closes after one response");
        std.string::string unknown = await raw_exchange(endpoint, "BREW /pot HTTP/1.1\r\n\r\n");
        expect_prefix(&unknown, "HTTP/1.1 501 Not Implemented\r\n");
        std.string::string version = await raw_exchange(endpoint, "GET / HTTP/1.2\r\n\r\n");
        expect_prefix(&version, "HTTP/1.1 505 HTTP Version Not Supported\r\n");
        std.string::string both = await raw_exchange(endpoint,
            "POST /echo HTTP/1.1\r\nContent-Length: 3\r\nTransfer-Encoding: chunked\r\n\r\nabc");
        expect_prefix(&both, "HTTP/1.1 400 Bad Request\r\n");
        std.string::string folded = await raw_exchange(endpoint,
            "GET /hello HTTP/1.1\r\nX-A: 1\r\n  folded\r\n\r\n");
        expect_prefix(&folded, "HTTP/1.1 400 Bad Request\r\n");
        std.string::string pipelined = await raw_exchange(endpoint,
            "GET /hello HTTP/1.1\r\n\r\nGET /items/9 HTTP/1.1\r\nConnection: close\r\n\r\n");
        std.test::check(std.text::ends_with(pipelined, "item 9"), "two pipelined requests");
        std.test::check(std.text::contains(pipelined, "\r\n\r\nhello"), "the first answer");
        checked += 1u32;
    }
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return checked;
}

@test
async void answers_requests_it_cannot_serve()
    throws std.error::fault, std.test::failure, std.http::http_error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Counter state = new arc Counter {.hits = 0u32};
    std.service::options settings = {};
    std.http::limits bounds = {.max_head = 256usize, .max_body = 16usize};
    task_scope(2) group {
        auto server = std.http::serve(move listener, settings, move stop, move state, routes(),
                                      bounds);
        auto client = protocol_clients(endpoint, move stopper);
        u32 checked = await move client;
        std.service::report account = await move server;
        std.test::equal(checked, 1u32);
        std.test::equal(account.failed, 0u64);
    }
}

@test
void keeps_header_sections() throws std.test::failure, std.http::http_error, std.alloc::alloc_error {
    std.http::headers fields = std.http::headers::create();
    fields.add("Accept", " text/html ");
    fields.add("Connection", "keep-alive, Upgrade");
    fields.add("accept", "application/json");
    std.test::equal(fields.count(), 3usize);
    switch (fields.get("ACCEPT")) {
    case variant o::some(value): std.test::equal_text(*value, "text/html");
    case variant o::none: std.test::fail("Accept");
    }
    std.test::check(fields.has_token("connection", "upgrade"), "a token of a list");
    std.test::check(fields.has_token("Connection", "close") == false, "an absent token");
    std.test::equal(fields.remove("Accept"), 2usize);
    fields.set("Connection", "close");
    std.test::equal(fields.count(), 1usize);
    std.test::equal_text(fields.name_at(0usize), "Connection");
    std.test::equal_text(fields.value_at(0usize), "close");
    try {
        fields.add("Bad Name", "x");
        std.test::fail("a name with a space");
    } catch (std.http::http_error rejected) {
        std.test::check(rejected.code == std.http::error_code::invalid_header, "invalid_header");
    }
    try {
        fields.add("X-Injected", "a\r\nb: c");
        std.test::fail("a value with a line end");
    } catch (std.http::http_error rejected) {
        std.test::check(rejected.code == std.http::error_code::invalid_header, "invalid_header");
    }
    std.http::request message = std.http::request::create(std.http::method::get, "/search?q=r&x=1");
    std.test::equal_text(message.path(), "/search");
    switch (message.query()) {
    case variant o::some(value): std.test::equal_text(*value, "q=r&x=1");
    case variant o::none: std.test::fail("query");
    }
    std.test::equal_text(std.http::method_name(std.http::method::delete), "DELETE");
    switch (std.http::parse_method("PATCH")) {
    case variant o::some(value): std.test::check(*value == std.http::method::patch, "PATCH");
    case variant o::none: std.test::fail("PATCH");
    }
    std.test::equal_text(std.http::reason(418u16), "");
    std.test::equal_text(std.http::reason(308u16), "Permanent Redirect");
    std.http::response answer = std.http::response::json(200u16, "{}");
    switch (answer.headers.get("Content-Type")) {
    case variant o::some(value): std.test::equal_text(*value, "application/json");
    case variant o::none: std.test::fail("Content-Type");
    }
}

/* Writes a chunked response on a connection of the pair and reads it with read_response. */
protected async u32 chunked_pair(std.net::tcp_stream client, std.net::tcp_connection server)
    throws std.error::fault, std.test::failure, std.http::http_error {
    std.http::response head = std.http::response::create(200u16);
    head.headers.add("Content-Type", "text/plain");
    std.bufio::reader<std.net::tcp_stream> input =
        std.bufio::reader<std.net::tcp_stream>::create(move client, 1024usize);
    std.http::limits bounds = {};
    std.string::string first = std.string::from_str("stream");
    std.string::string second = std.string::from_str("ed body");
    u32 checked = 0u32;
    task_scope(1) io {
        await std.http::write_chunked_head(&server, &head, false);
        await std.http::write_chunk(&server, first);
        await std.http::write_chunk(&server, second);
        await std.http::finish_chunks(&server);
        std.http::response result = await std.http::read_response(&input, std.http::method::get, &bounds);
        std.test::equal(result.status, 200u16);
        std.test::check(std.bytes::equal(result.body.as_slice(), "streamed body"), "chunks");
        checked += 1u32;
    }
    return checked;
}

@test
async void writes_and_reads_chunked_bodies()
    throws std.error::fault, std.test::failure, std.http::http_error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect();
    std.net::tcp_connection server = await listener.accept();
    await (move listener).close();
    u32 checked = await chunked_pair(move client, move server);
    std.test::equal(checked, 1u32);
}

@test
void formats_and_parses_event_streams() throws std.test::failure, std.http::http_error, std.alloc::alloc_error {
    std.string::string one = std.http::sse_event("update", "7", "a\nb\r\nc");
    std.test::equal_text(one, "event: update\nid: 7\ndata: a\ndata: b\ndata: c\n\n");
    std.string::string plain = std.http::sse_event("", "", "");
    std.test::equal_text(plain, "data: \n\n");
    std.http::sse_parser parser = std.http::sse_parser::create();
    parser.feed(": comment\r\nevent: first\rdata: x\r");
    std.test::check(drained(&parser), "a CR may precede an LF");
    parser.feed("\ndata:y\n\nid: 9\ndata: z\nretry: 5\n\n\nevent: skipped\n\ndata: last\n");
    switch (parser.next()) {
    case variant o::some(message):
        std.test::equal_text(message->event, "first");
        std.test::equal_text(message->data, "x\ny");
        std.test::equal_text(message->id, "");
    case variant o::none: std.test::fail("the first message");
    }
    switch (parser.next()) {
    case variant o::some(message):
        std.test::equal_text(message->event, "message");
        std.test::equal_text(message->data, "z");
        std.test::equal_text(message->id, "9");
    case variant o::none: std.test::fail("the second message");
    }
    std.test::check(drained(&parser), "an unfinished message waits");
    parser.feed("\n");
    switch (parser.next()) {
    case variant o::some(message):
        std.test::equal_text(message->event, "message");
        std.test::equal_text(message->data, "last");
    case variant o::none: std.test::fail("the last message");
    }
}

protected void add_header(std.http::headers* fields, str name, str value) throws std.alloc::alloc_error {
    try {
        fields->add(name, value);
    } catch (std.http::http_error rejected) {
        rejected as void;
    }
}

/* Sends back what the connection reads, the bytes read with the head first, until its end. */
@scoped
protected async void echo_stream(const std.http::upgraded* switched) throws std.error::fault {
    u8[64] chunk = {};
    task_scope(1) io {
        if (len(switched->buffered) != 0usize) {
            await switched->transport.write_all_from(switched->buffered.as_slice());
        }
        while (true) {
            usize count = await switched->transport.read_into(&chunk);
            if (count == 0usize) { break; }
            await switched->transport.write_all_from(chunk[0usize..count]);
        }
        await switched->transport.shutdown();
    }
}

/* Accepts an upgrade to the "echo" protocol and refuses any other request with 400. */
protected async void echo_upgrade(arc Counter state, std.http::request incoming,
                                  std.http::upgrade connection)
    throws std.error::fault {
    drop state;
    if (incoming.headers.has_token("Upgrade", "echo") == false) {
        await (move connection).refuse(std.http::response::text(400u16, "echo only"));
        return;
    }
    std.http::headers fields = std.http::headers::create();
    add_header(&fields, "Upgrade", "echo");
    add_header(&fields, "Connection", "Upgrade");
    std.http::upgraded switched = await (move connection).accept(move fields);
    task_scope(1) io { await echo_stream(&switched); }
}

protected std.http::router<Counter> upgrade_routes() throws std.http::http_error, std.alloc::alloc_error {
    std.http::router<Counter> result = std.http::router<Counter>::create();
    result.add(std.http::method::get, "/hello", hello);
    result.add_upgrade(std.http::method::get, "/echo", echo_upgrade);
    return move result;
}

/* Writes the text on the connection, ends its write direction and returns what comes back. */
@scoped
protected async std.string::string talk(const std.http::upgraded* switched, str text)
    throws std.error::fault {
    u8[16] chunk = {};
    std.string::string received = std.string::create();
    task_scope(1) io {
        await switched->transport.write_all_from(text);
        await switched->transport.shutdown();
        while (true) {
            usize count = await switched->transport.read_into(&chunk);
            if (count == 0usize) { break; }
            std.string::append_utf8(&received, chunk[0usize..count]);
        }
    }
    return move received;
}

protected async u32 upgrade_requests(std.string::string base, std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.test::failure, std.http::http_error, std.tls::tls_error {
    std.http::client_options client_settings = {};
    std.http::client web = std.http::client::create(client_settings);
    std.string::string echo_url = url_at(&base, "/echo");
    std.string::string hello_url = url_at(&base, "/hello");
    u32 checked = 0u32;
    std.http::request plain = std.http::request::create(std.http::method::get, "/");
    std.http::request asking = std.http::request::create(std.http::method::get, "/");
    add_header(&asking.headers, "Upgrade", "echo");
    add_header(&asking.headers, "Connection", "Upgrade");
    o<std.http::upgraded> taken = o::none;
    task_scope(1) io {
        std.http::handshake refused = await web.upgrade(move plain, echo_url);
        expect_text(&refused.answer, 400u16, "echo only");
        o<std.http::upgraded> none = core::replace(&refused.connection, o::none);
        switch (move none) {
        case variant o::some(move unexpected):
            drop unexpected;
            std.test::fail("a refused upgrade keeps no connection");
        case variant o::none: checked += 1u32;
        }
        std.http::handshake accepted = await web.upgrade(move asking, echo_url);
        std.test::equal(accepted.answer.status, 101u16);
        std.test::check(accepted.answer.headers.has_token("Upgrade", "echo"), "the protocol");
        std.test::check(accepted.answer.headers.contains("Date"), "a Date field");
        o<std.http::upgraded> received = core::replace(&accepted.connection, o::none);
        o<std.http::upgraded> old = core::replace(&taken, move received);
        drop old;
    }
    switch (move taken) {
    case variant o::some(move switched):
        task_scope(1) io {
            std.string::string back = await talk(&switched, "over the switched connection");
            std.test::equal_text(back, "over the switched connection");
            checked += 1u32;
        }
    case variant o::none: std.test::fail("an accepted upgrade keeps the connection");
    }
    task_scope(1) io {
        std.http::handshake ordinary = await web.upgrade(
            std.http::request::create(std.http::method::get, "/"), hello_url);
        expect_text(&ordinary.answer, 200u16, "hello");
        checked += 1u32;
    }
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return checked;
}

@test
async void upgrades_connections()
    throws std.error::fault, std.test::failure, std.http::http_error, std.tls::tls_error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Counter state = new arc Counter {.hits = 0u32};
    std.service::options settings = {};
    std.http::limits bounds = {};
    task_scope(2) group {
        auto server = std.http::serve(move listener, settings, move stop, move state,
                                      upgrade_routes(), bounds);
        auto client = upgrade_requests(base_of(endpoint, "http"), move stopper);
        u32 checked = await move client;
        std.service::report account = await move server;
        std.test::equal(checked, 3u32);
        std.test::equal(account.failed, 0u64);
    }
}

// ---- Applications (R-SLIB-HTTP-0015..0018) ----

/* The context of a request of the application tests: the account its token named and the digits
   of the stages it passed. */
protected struct Visit { std.string::string account; u32 trail; };

protected Visit open_visit(const Counter* state, const std.http::request* incoming) {
    state as void;
    incoming as void;
    return Visit {.account = std.string::create(), .trail = 0u32};
}

protected std.http::flow<Visit> step(std.http::flow<Visit> current, u32 digit) {
    current.context.trail = current.context.trail * 10u32 + digit;
    return move current;
}

/* A before hook that reads the account of the token cookie or answers 401. */
protected async std.http::flow<Visit> sign_in(arc Counter state, std.http::flow<Visit> current)
    throws std.error::fault {
    drop state;
    o<std.string::string> token = o::none;
    switch (current.request.cookie("token")) {
    case variant o::some(value): token = o::some(std.string::from_str(*value));
    case variant o::none: break;
    }
    switch (move token) {
    case variant o::some(move name):
        current.notes.set("account", name);
        std.string::append_str(&current.context.account, name);
        return step(move current, 1u32);
    case variant o::none:
        std.http::flow<Visit> refused = step(move current, 9u32);
        return (move refused).with(std.http::response::text(401u16, "no token"));
    }
}

/* An after hook that writes the trail of the request into a field of its response. */
protected async std.http::flow<Visit> stamp(arc Counter state, std.http::flow<Visit> current)
    throws std.error::fault {
    drop state;
    std.http::flow<Visit> passed = step(move current, 3u32);
    u32 trail = passed.context.trail;
    std.string::string text = f"{trail}";
    add_field(&passed.response, "X-Trail", text);
    return move passed;
}

protected async std.http::flow<Visit> show_user(arc Counter state, std.http::flow<Visit> current)
    throws std.error::fault {
    drop state;
    std.http::flow<Visit> handled = step(move current, 2u32);
    std.string::string body = std.string::from_str("user ");
    switch (handled.request.param("id")) {
    case variant o::some(id): std.string::append_str(&body, *id);
    case variant o::none: break;
    }
    return (move handled).with(std.http::response::text(200u16, body));
}

protected async std.http::flow<Visit> show_me(arc Counter state, std.http::flow<Visit> current)
    throws std.error::fault {
    drop state;
    std.http::flow<Visit> handled = step(move current, 4u32);
    std.string::string body = std.string::from_str("me ");
    std.string::append_str(&body, handled.context.account);
    return (move handled).with(std.http::response::text(200u16, body));
}

protected async std.http::flow<Visit> show_all(arc Counter state, std.http::flow<Visit> current)
    throws std.error::fault {
    drop state;
    return (move current).with(std.http::response::text(200u16, "all"));
}

protected async std.http::flow<Visit> refuse(arc Counter state, std.http::flow<Visit> current)
    throws std.error::fault {
    drop state;
    drop current;
    throw std.alloc::alloc_error::out_of_memory;
}

protected u8 element(const u8[] values, usize at) { return values[at]; }

protected async std.http::flow<Visit> crash(arc Counter state, std.http::flow<Visit> current)
    throws std.error::fault {
    drop state;
    u8[2] values = {1u8, 2u8};
    u8 read = element(values[0usize..2usize], 5usize);
    read as void;
    return move current;
}

/* The panics the hook of the application received, with a digit per failed check. */
atomic u32 panics = 0u32;

protected void note_panic(const Counter* state, std.http::method sent, str target, const std.http::notes* noted,
                          const std.thread::panic_report* report) {
    state as void;
    u32 seen = 1u32;
    try {
        o<std.string::string> account = noted->get("account");
        switch (move account) {
        case variant o::some(move named):
            if (std.bytes::equal(named, "ann") == false) { seen = 100u32; }
            drop named;
        case variant o::none: seen = 100u32;
        }
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        seen = 100u32;
    }
    if (sent != std.http::method::get) { seen = 100u32; }
    if (std.bytes::equal(target, "/api/crash") == false) { seen = 100u32; }
    constexpr str category = std.thread::panic_category(report);
    if (std.bytes::equal(category, "bounds") == false) { seen = 100u32; }
    core::atomic_fetch_add(&panics, seen, core::memory_order::relaxed) as void;
}

protected std.http::app<Counter, Visit> application() throws std.http::http_error, std.alloc::alloc_error {
    std.http::app<Counter, Visit> served = std.http::app<Counter, Visit>::create(open_visit);
    served.around("/api", sign_in, stamp);
    served.route(std.http::method::get, "/api/users/{id}", show_user);
    served.route(std.http::method::get, "/api/users/me", show_me);
    served.route(std.http::method::get, "/api/*", show_all);
    served.route(std.http::method::post, "/api/orders", show_user);
    served.route(std.http::method::get, "/api/refuse", refuse);
    served.route(std.http::method::get, "/api/crash", crash);
    served.route(std.http::method::get, "/shop/", show_all);
    served.on_panic(note_panic);
    return move served;
}

protected std.http::request with_cookie(std.http::method value, str target, str cookie)
    throws std.http::http_error, std.alloc::alloc_error {
    std.http::request made = std.http::request::create(value, target);
    const u8[] bytes = cookie;
    if (len(bytes) != 0usize) { made.headers.add("Cookie", cookie); }
    return move made;
}

protected void expect_field(const std.http::response* result, str name, str value)
    throws std.test::failure, std.alloc::alloc_error {
    switch (result->headers.get(name)) {
    case variant o::some(found): std.test::equal_text(*found, value);
    case variant o::none: std.test::fail(name);
    }
}

protected void expect_trail(const std.http::response* result, str trail)
    throws std.test::failure, std.alloc::alloc_error {
    switch (result->headers.get("X-Trail")) {
    case variant o::some(value): std.test::equal_text(*value, trail);
    case variant o::none: std.test::fail(trail);
    }
}

// R-SLIB-HTTP-0015, R-SLIB-HTTP-0018: hooks in order and back, the context, the most specific
// route whatever the order of the calls, 405 and 404, the redirect of a trailing slash and the
// answer to a handler that throws.
@test
async void applications_run_middleware_and_routes()
    throws std.error::fault, std.test::failure, std.http::http_error {
    std.http::app<Counter, Visit> served = application();
    arc Counter state = new arc Counter {.hits = 0u32};
    task_scope(1) calls {
        std.http::response user = await served.dispatch(std.arc::clone(&state),
                                                        with_cookie(std.http::method::get, "/api/users/42", "token=ann"));
        expect_text(&user, 200u16, "user 42");
        expect_trail(&user, "123");
        std.http::response me = await served.dispatch(std.arc::clone(&state),
                                                      with_cookie(std.http::method::get, "/api/users/me", "a=b; token=\"ann\""));
        expect_text(&me, 200u16, "me ann");
        expect_trail(&me, "143");
        std.http::response other = await served.dispatch(std.arc::clone(&state),
                                                         with_cookie(std.http::method::get, "/api/x/y", "token=ann"));
        expect_text(&other, 200u16, "all");
        std.http::response anonymous = await served.dispatch(std.arc::clone(&state),
                                                             with_cookie(std.http::method::get, "/api/users/me", ""));
        expect_text(&anonymous, 401u16, "no token");
        expect_trail(&anonymous, "93");
        std.http::response wrong = await served.dispatch(std.arc::clone(&state),
                                                         with_cookie(std.http::method::delete, "/api/orders", "token=ann"));
        std.test::equal(wrong.status, 405u16);
        switch (wrong.headers.get("Allow")) {
        // The pattern `/api/*` of a GET route matches the path too.
        case variant o::some(value): std.test::equal_text(*value, "GET, POST");
        case variant o::none: std.test::fail("Allow");
        }
        std.http::response refused = await served.dispatch(std.arc::clone(&state),
                                                           with_cookie(std.http::method::get, "/api/refuse", "token=ann"));
        std.test::equal(refused.status, 500u16);
        std.http::response missing = await served.dispatch(std.arc::clone(&state),
                                                           with_cookie(std.http::method::get, "/shop", ""));
        expect_text(&missing, 404u16, "Not Found");
    }
    served.redirect_trailing_slash(true);
    served.method_not_allowed(false);
    task_scope(1) settings {
        std.http::response redirected = await served.dispatch(std.arc::clone(&state),
                                                         with_cookie(std.http::method::get, "/shop?page=2", ""));
        std.test::equal(redirected.status, 301u16);
        switch (redirected.headers.get("Location")) {
        case variant o::some(value): std.test::equal_text(*value, "/shop/?page=2");
        case variant o::none: std.test::fail("Location");
        }
        std.http::response gone = await served.dispatch(std.arc::clone(&state),
                                                        with_cookie(std.http::method::delete, "/api/orders", "token=ann"));
        std.test::equal(gone.status, 404u16);
    }
}

// R-SLIB-HTTP-0015: a handler that panics is answered with 500, its report goes to the hook and
// the application answers the next request.
@test
async void applications_isolate_a_panicking_handler()
    throws std.error::fault, std.test::failure, std.http::http_error {
    std.http::app<Counter, Visit> served = application();
    arc Counter state = new arc Counter {.hits = 0u32};
    core::atomic_store(&panics, 0u32, core::memory_order::relaxed);
    task_scope(1) calls {
        std.http::response crashed = await served.dispatch(std.arc::clone(&state),
                                                           with_cookie(std.http::method::get, "/api/crash", "token=ann"));
        expect_text(&crashed, 500u16, "Internal Server Error");
        std.http::response after = await served.dispatch(std.arc::clone(&state),
                                                         with_cookie(std.http::method::get, "/api/users/7", "token=bob"));
        expect_text(&after, 200u16, "user 7");
    }
    std.test::equal(core::atomic_load(&panics, core::memory_order::relaxed), 1u32);
}

// R-SLIB-HTTP-0016, R-SLIB-HTTP-0017: cookies, and cross-origin requests of an allowed and of
// another origin.
@test
async void applications_answer_cors_and_cookies()
    throws std.error::fault, std.test::failure, std.http::http_error {
    std.http::app<Counter, Visit> served = application();
    std.http::cors_policy policy = {.credentials = true, .max_age = 60u32};
    try {
        policy.origins.push(std.string::from_str("https://game.example"));
    } catch (std.array::push_error<std.string::string> rejected) {
        (move rejected) as void;
        std.test::fail("an origin");
    }
    served.cors(move policy);
    arc Counter state = new arc Counter {.hits = 0u32};
    task_scope(1) calls {
        std.http::request asked = std.http::request::create(std.http::method::options, "/api/users/1");
        asked.headers.add("Origin", "https://game.example");
        asked.headers.add("Access-Control-Request-Method", "PUT");
        asked.headers.add("Access-Control-Request-Headers", "X-Command");
        std.http::response preflight = await served.dispatch(std.arc::clone(&state), move asked);
        std.test::equal(preflight.status, 204u16);
        expect_field(&preflight, "Access-Control-Allow-Origin", "https://game.example");
        expect_field(&preflight, "Access-Control-Allow-Credentials", "true");
        expect_field(&preflight, "Access-Control-Allow-Methods", "PUT");
        expect_field(&preflight, "Access-Control-Allow-Headers", "X-Command");
        expect_field(&preflight, "Access-Control-Max-Age", "60");
        std.http::request foreign = std.http::request::create(std.http::method::options, "/api/users/1");
        foreign.headers.add("Origin", "https://other.example");
        foreign.headers.add("Access-Control-Request-Method", "GET");
        std.http::response refused = await served.dispatch(std.arc::clone(&state), move foreign);
        std.test::equal(refused.status, 403u16);
        std.http::request plain = with_cookie(std.http::method::get, "/api/users/5", "token=ann");
        plain.headers.add("Origin", "https://game.example");
        std.http::response marked = await served.dispatch(std.arc::clone(&state), move plain);
        expect_text(&marked, 200u16, "user 5");
        expect_field(&marked, "Access-Control-Allow-Origin", "https://game.example");
        expect_field(&marked, "Vary", "Origin");
    }
    std.http::cookie session = {.name = std.string::from_str("token"), .value = std.string::from_str("t1"),
                                .path = o::some(std.string::from_str("/")), .max_age = o::some(3600i64),
                                .secure = true, .http_only = true,
                                .same_site = o::some(std.string::from_str("Lax"))};
    std.http::response answer = std.http::response::create(200u16);
    answer.set_cookie(&session);
    switch (answer.headers.get("Set-Cookie")) {
    case variant o::some(value):
        std.test::equal_text(*value, "token=t1; Path=/; Max-Age=3600; Secure; HttpOnly; SameSite=Lax");
    case variant o::none: std.test::fail("Set-Cookie");
    }
}

protected async u32 application_clients(std.net::socket_address endpoint,
                                         std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.test::failure, std.http::http_error {
    std.net::tcp_stream stream = await endpoint.connect();
    std.bufio::reader<std.net::tcp_stream> connection =
        std.bufio::reader<std.net::tcp_stream>::create(move stream, 4096usize);
    u32 checked = 0u32;
    task_scope(1) io {
        std.http::response first = await call(&connection, with_cookie(std.http::method::get, "/api/users/1", "token=ann"));
        expect_text(&first, 200u16, "user 1");
        std.test::check(first.headers.contains("Date"), "a Date field");
        std.http::response crashed = await call(&connection, with_cookie(std.http::method::get, "/api/crash", "token=ann"));
        expect_text(&crashed, 500u16, "Internal Server Error");
        std.http::response after = await call(&connection, with_cookie(std.http::method::get, "/api/users/me", "token=bob"));
        expect_text(&after, 200u16, "me bob");
        checked += 1u32;
    }
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return checked;
}

// R-SLIB-HTTP-0015: serve_app answers on a kept-alive connection, which a panicking handler does
// not end.
@test
async void serves_applications()
    throws std.error::fault, std.test::failure, std.http::http_error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Counter state = new arc Counter {.hits = 0u32};
    array<std.service::listener> listeners = [];
    try {
        listeners.push(std.service::listener::tcp(move listener));
    } catch (std.array::push_error<std.service::listener> rejected) {
        drop rejected;
        throw std.alloc::alloc_error::out_of_memory;
    }
    std.service::options settings = {};
    std.http::limits bounds = {};
    core::atomic_store(&panics, 0u32, core::memory_order::relaxed);
    task_scope(2) group {
        auto server = std.http::serve_app(move listeners, settings, move stop, std.service::health::create(),
                                          move state, application(), bounds);
        auto client = application_clients(endpoint, move stopper);
        u32 checked = await move client;
        std.service::report account = await move server;
        std.test::equal(checked, 1u32);
        std.test::equal(account.accepted, 1u64);
        std.test::equal(account.failed, 0u64);
    }
    std.test::equal(core::atomic_load(&panics, core::memory_order::relaxed), 1u32);
}
