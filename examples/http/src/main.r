module example.http.main;
import std.console;
import std.http;
import std.service;
import std.tls;
import example.http.catalog;
import example.http.texts;
import example.http.wire;

protected async bytes read_pem(std.string::string name) throws std.error::fault {
    std.fs::path path = std.fs::path_from_utf8(name);
    return await path.read_file(65536usize);
}

protected async std.net::tcp_listener listen_loopback() throws std.error::fault {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 8u32, .reuse_address = true, .v6_only = false};
    return await local.listen(options);
}

protected std.string::string base_url(str scheme, std.net::socket_address endpoint)
    throws std.alloc::alloc_error {
    u16 port = endpoint.port;
    return f"{scheme}://localhost:{port}";
}

/* Sends one request and prints "SCHEME METHOD PATH -> STATUS BODY". */
@scoped
protected async void show(std.http::client* web, str scheme, const std.string::string* base,
                          std.http::request message)
    throws std.error::fault, std.http::http_error, std.tls::tls_error {
    std.string::string address = std.string::from_str(base->as_str());
    std.string::append_str(&address, message.target);
    str sent = std.http::method_name(message.method);
    std.string::string path = std.string::from_str(message.target);
    task_scope(1) io {
        std.http::response result = await web->send(move message, address);
        u16 status = result.status;
        std.string::string body = std.string::from_utf8(result.body.as_slice());
        await std.console::println(f"{scheme} {sent} {path} -> {status} {body}");
    }
}

protected std.http::request post_item(str body, bool keyed)
    throws std.http::http_error, std.alloc::alloc_error {
    std.http::request message = std.http::request::create(std.http::method::post, "/items");
    message.headers.add("Content-Type", "application/json");
    if (keyed == true) { message.headers.add("X-Api-Key", "secret"); }
    std.bytes::append(&message.body, body);
    return move message;
}

protected std.http::request get(str target) throws std.alloc::alloc_error {
    return std.http::request::create(std.http::method::get, target);
}

/* The events that the parser has complete, one line each. */
protected std.string::string event_lines(std.http::sse_parser* parser, str scheme)
    throws std.http::http_error, std.alloc::alloc_error {
    std.string::string lines = std.string::create();
    // A pattern test as the loop condition: the loop ends at the first o::none (R-STMT-0002).
    while (parser->next() is variant o::some(move message)) {
        std.string::string line = f"{scheme} event {message.event} id={message.id} {message.data}\n";
        std.string::append_str(&lines, line);
    }
    return move lines;
}

/* Opens the event stream of prices and prints each event as its bytes arrive. */
@scoped
protected async void show_events(std.http::client* web, str scheme, const std.string::string* base)
    throws std.error::fault, std.http::http_error, std.tls::tls_error {
    std.string::string address = std.string::from_str(base->as_str());
    std.string::append_str(&address, "/prices");
    o<std.http::streamed> opened = o::none;
    task_scope(1) io {
        std.http::streamed received = await web->open(get("/prices"), address);
        o<std.http::streamed> old = core::replace(&opened, o::some(move received));
        drop old;
    }
    switch (move opened) {
    case variant o::some(move stream):
        std.http::sse_parser parser = std.http::sse_parser::create();
        bool more = true;
        while (more == true) {
            o<bytes> arrived = o::none;
            task_scope(1) io {
                o<bytes> got = await stream.next();
                o<bytes> old = core::replace(&arrived, move got);
                drop old;
            }
            switch (move arrived) {
            case variant o::some(move data):
                parser.feed(data.as_slice());
                std.string::string lines = event_lines(&parser, scheme);
                task_scope(1) print { await std.console::print(move lines); }
            case variant o::none: more = false;
            }
        }
        drop parser;
        drop stream;
    case variant o::none: break;
    }
}

/* The requests over HTTP: reads, a created item, refused items and a redirect. */
@scoped
protected async void plain_requests(std.http::client* web, const std.string::string* base)
    throws std.error::fault, std.http::http_error, std.tls::tls_error {
    task_scope(1) reads {
        await show(web, "http", base, get("/health"));
        await show(web, "http", base, get("/items/2"));
        await show(web, "http", base, get("/items/9"));
    }
    task_scope(1) writes {
        await show(web, "http", base, post_item("{\"name\":\"desk\",\"price\":120}", false));
        await show(web, "http", base, post_item("{\"name\":\"desk\",\"price\":120}", true));
        await show(web, "http", base, post_item("{\"name\":\"desk\"}", true));
        await show(web, "http", base, get("/old-items"));
    }
}

/* The requests over HTTPS and its event stream. */
@scoped
protected async void secure_requests(std.http::client* web, const std.string::string* base)
    throws std.error::fault, std.http::http_error, std.tls::tls_error {
    task_scope(1) io {
        await show(web, "https", base, get("/health"));
        await show(web, "https", base, get("/items/3"));
        await show_events(web, "https", base);
    }
}

/* The requests of the demonstration over HTTP, then over HTTPS; then both servers stop. */
protected async void conversation(std.string::string plain_base, std.string::string secure_base,
                                  std.http::client owner,
                                  std.sync::sender<std.service::stop> plain_stop,
                                  std.sync::sender<std.service::stop> secure_stop)
    throws std.error::fault, std.http::http_error, std.tls::tls_error {
    std.http::client web = move owner;
    task_scope(1) io {
        await plain_requests(&web, &plain_base);
        await secure_requests(&web, &secure_base);
    }
    std.sync::send_result<std.service::stop> first = std.sync::send(&plain_stop, std.service::stop::drain);
    drop first;
    std.sync::send_result<std.service::stop> second = std.sync::send(&secure_stop, std.service::stop::drain);
    drop second;
}

protected async i32 demo(array<std.string::string> arguments)
    throws std.error::fault, std.http::http_error, std.tls::tls_error {
    bytes authority = await read_pem(core::replace(&arguments[2], std.string::create()));
    bytes chain = await read_pem(core::replace(&arguments[3], std.string::create()));
    bytes secret = await read_pem(core::replace(&arguments[4], std.string::create()));
    std.secret::buffer key = std.secret::from_bytes(move secret);
    std.tls::config server_tls = std.tls::server_config();
    server_tls.set_identity(chain.as_slice(), &key);
    std.tls::config client_tls = std.tls::client_config();
    client_tls.add_authority(authority.as_slice());
    std.net::tcp_listener plain = await listen_loopback();
    std.net::tcp_listener secure = await listen_loopback();
    std.string::string plain_base = base_url("http", plain.local_address());
    std.string::string secure_base = base_url("https", secure.local_address());
    std.sync::channel<std.service::stop> plain_channel = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> plain_stopper = std.sync::sender(&plain_channel);
    std.sync::receiver<std.service::stop> plain_stop = std.sync::receiver(move plain_channel);
    std.sync::channel<std.service::stop> secure_channel = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> secure_stopper = std.sync::sender(&secure_channel);
    std.sync::receiver<std.service::stop> secure_stop = std.sync::receiver(move secure_channel);
    arc example.http.catalog::Catalog state = new arc example.http.catalog::Catalog(
        example.http.catalog::create());
    std.service::options settings = {};
    std.http::limits bounds = {};
    std.http::client_options client_settings = {};
    arc std.tls::config shared_tls = new arc std.tls::config(move server_tls);
    arc std.tls::config verifying = new arc std.tls::config(move client_tls);
    task_scope(3) group {
        auto http_server = std.http::serve(move plain, settings, move plain_stop,
                                           std.arc::clone(&state), example.http.catalog::routes(),
                                           bounds);
        auto https_server = std.http::serve_tls(move secure, settings, move secure_stop,
                                                std.arc::clone(&state),
                                                example.http.catalog::routes(), bounds,
                                                move shared_tls);
        auto talk = conversation(move plain_base, move secure_base,
                                 std.http::client::with_tls(client_settings, move verifying),
                                 move plain_stopper, move secure_stopper);
        await move talk;
        std.service::report plain_report = await move http_server;
        std.service::report secure_report = await move https_server;
        u64 plain_count = plain_report.accepted;
        u64 secure_count = secure_report.accepted;
        await std.console::println(f"connections: http {plain_count}, https {secure_count}");
    }
    return 0;
}

protected str usage_text() {
    return "http demo CA CERT KEY\nhttp url URL [REFERENCE]\nhttp mime TYPE|PATH\nhttp wire\n";
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    str command = "";
    if (given >= 2usize) { command = arguments[1]; }
    bool demo_call = std.bytes::equal(command, "demo") == true && given == 5usize;
    bool url_call = std.bytes::equal(command, "url") == true && (given == 3usize || given == 4usize);
    bool mime_call = std.bytes::equal(command, "mime") == true && given == 3usize;
    bool wire_call = std.bytes::equal(command, "wire") == true && given == 2usize;
    if (demo_call == false && url_call == false && mime_call == false && wire_call == false) {
        drop arguments;
        await std.console::eprint(std.string::from_str(usage_text()));
        if (given == 1usize) { return 0; }
        return 64;
    }
    try {
        if (url_call == true) {
            std.string::string text = core::replace(&arguments[2], std.string::create());
            o<std.string::string> reference = o::none;
            if (given == 4usize) { reference = o::some(core::replace(&arguments[3], std.string::create())); }
            drop arguments;
            return await example.http.texts::show_url(move text, move reference);
        }
        if (mime_call == true) {
            std.string::string text = core::replace(&arguments[2], std.string::create());
            drop arguments;
            return await example.http.texts::show_mime(move text);
        }
        if (wire_call == true) {
            drop arguments;
            return await example.http.wire::wire();
        }
        return await demo(move arguments);
    } catch (std.http::http_error failure) {
        await std.console::eprintln(f"http: {failure.code}");
    } catch (std.tls::tls_error failure) {
        await std.console::eprintln(f"tls: {failure.code}");
    }
    return 65;
}
