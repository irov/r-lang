module example.telemetry.main;
import std.console;
import std.net;
import std.tls;
import std.http;
import std.service;
import std.metrics;
import std.trace;
import std.signal;
import example.telemetry.service;

/* telemetry serves HTTP on TCP, TLS and a Unix-domain socket at once, exports its metrics at
   /metrics, reports its health at /health and its spans at /traces, closes connections that stay
   idle, and stops with a drain on SIGTERM or SIGINT. */

const usize max_pem = 65536usize;

protected async bytes read_pem(std.string::string name) throws std.error::fault {
    std.fs::path path = std.fs::path_from_utf8(name);
    return await path.read_file(max_pem);
}

/* The TLS configuration of the server from PEM files of its chain and key. */
protected async std.tls::config server_tls(std.string::string cert, std.string::string key)
    throws std.error::fault, std.tls::tls_error {
    bytes chain = await read_pem(move cert);
    bytes secret = await read_pem(move key);
    std.secret::buffer private_key = std.secret::from_bytes(move secret);
    std.tls::config configured = std.tls::server_config();
    configured.set_identity(chain.as_slice(), &private_key);
    return move configured;
}

protected async std.net::tcp_listener listen_on(u16 port) throws std.error::fault {
    std.net::socket_address local = std.net::socket_address {
        .address = std.net::parse_ip("127.0.0.1"), .port = port, .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 16u32, .reuse_address = true, .v6_only = false};
    return await local.listen(options);
}

protected void add_listener(array<std.service::listener>* listeners, std.service::listener item)
    throws std.alloc::alloc_error {
    try {
        listeners->push(move item);
    } catch (std.array::push_error<std.service::listener> rejected) {
        drop rejected;
        throw std.alloc::alloc_error::out_of_memory;
    }
}

/* The routes of the service, its health and its metrics endpoint. */
protected std.http::router<example.telemetry.service::Telemetry> routes(const std.service::health* status,
                                                                        const std.metrics::registry* registry)
    throws std.http::http_error, std.alloc::alloc_error {
    std.http::router<example.telemetry.service::Telemetry> table =
        std.http::router<example.telemetry.service::Telemetry>::create();
    table.add(std.http::method::get, "/hello", example.telemetry.service::hello);
    table.add(std.http::method::get, "/items/{id}", example.telemetry.service::item);
    table.add(std.http::method::get, "/traces", example.telemetry.service::traces);
    table.health("/health", status->share());
    table.metrics("/metrics", registry->share());
    return move table;
}

protected std.service::options settings() {
    return std.service::options {.capacity = 32u32, .timeout = std.time::duration_from_seconds(10i64),
                                 .idle_timeout = o::some(std.time::duration_from_seconds(2i64)),
                                 .stop_on_signals = true};
}

protected std.string::string summary(std.service::report account) throws std.alloc::alloc_error {
    u64 accepted = account.accepted;
    u64 completed = account.completed;
    u64 failed = account.failed;
    u64 rejected = account.rejected;
    u64 cancelled = account.cancelled;
    return f"stopped: accepted {accepted}, completed {completed}, failed {failed}, rejected {rejected}, cancelled {cancelled}";
}

/* serve HTTP_PORT TLS_PORT SOCKET CERT KEY: until SIGTERM or SIGINT. */
protected async i32 serve(u16 http_port, u16 tls_port, std.string::string socket, std.string::string cert,
                          std.string::string key) throws std.error::fault, std.tls::tls_error, std.http::http_error,
                                                         std.metrics::metrics_error {
    std.tls::config tls = await server_tls(move cert, move key);
    arc std.tls::config shared_tls = new arc std.tls::config(move tls);
    std.net::tcp_listener plain = await listen_on(http_port);
    std.net::tcp_listener secure = await listen_on(tls_port);
    std.net::unix_listener local = await std.net::unix_listen(socket, 16u32, true, o::none);
    array<std.service::listener> listeners = [];
    add_listener(&listeners, std.service::listener::tcp(move plain));
    add_listener(&listeners, std.service::listener::tls {.socket = move secure, .settings = move shared_tls});
    add_listener(&listeners, std.service::listener::unix(move local));
    std.metrics::registry registry = std.metrics::registry::create();
    std.trace::tracer tracer = std.trace::tracer::create(256usize);
    arc example.telemetry.service::Telemetry state =
        new arc example.telemetry.service::Telemetry(example.telemetry.service::create(&registry, move tracer));
    std.service::health status = std.service::health::create();
    std.http::router<example.telemetry.service::Telemetry> table = routes(&status, &registry);
    std.sync::channel<std.service::stop> channel = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&channel);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move channel);
    await std.console::println(f"ready http {http_port} tls {tls_port} unix {socket}");
    std.http::limits bounds = {};
    std.service::report account = await std.http::serve_all(move listeners, settings(), move stop, move status,
                                                            move state, move table, bounds);
    drop stopper;
    await std.console::println(summary(account));
    return 0;
}

/* One request over the Unix-domain socket, written by hand: the status line of its answer. */
protected async std.string::string unix_get(std.string::string socket, std.string::string target) throws std.error::fault {
    std.net::unix_stream stream = await std.net::unix_connect(socket, o::none);
    std.string::string request = f"GET {target} HTTP/1.1\r\nHost: local\r\nConnection: close\r\n\r\n";
    task_scope(1) sending { await std.net::unix_write_all_from(&stream, request, o::none); }
    bytes received = {};
    u8[512] buffer = {};
    bool open = true;
    while (open == true) {
        task_scope(1) reading {
            usize count = await std.net::unix_read_into(&stream, buffer[..], o::none);
            if (count == 0usize) { open = false; }
            std.bytes::append(&received, buffer[0usize..count]);
        }
    }
    await std.net::unix_close(move stream, o::none);
    const u8[] all = received.as_slice();
    usize end = len(all);
    for (usize index = 0usize; index + 1usize < len(all); index += 1usize) {
        if (all[index] == 13u8 && all[index + 1usize] == 10u8 && end == len(all)) { end = index; }
    }
    return std.string::from_utf8(all[0usize..end]);
}

/* The lines of text that start with prefix. */
protected std.string::string lines_with(str text, str prefix) throws std.alloc::alloc_error {
    std.string::string out = std.string::create();
    const u8[] all = text;
    const u8[] wanted = prefix;
    usize start = 0usize;
    for (usize index = 0usize; index <= len(all); index += 1usize) {
        if (index == len(all) || all[index] == 10u8) {
            const u8[] line = all[start..index];
            if (len(line) >= len(wanted) && std.bytes::equal(line[0usize..len(wanted)], wanted) == true) {
                try {
                    std.string::append_utf8(&out, line);
                } catch (std.string::string_error rejected) {
                    rejected as void;
                }
                std.string::append_str(&out, "\n");
            }
            start = index + 1usize;
        }
    }
    return move out;
}

protected usize count_lines(const u8[] text) {
    usize count = 0usize;
    for (usize index = 0usize; index < len(text); index += 1usize) {
        if (text[index] == 10u8) { count += 1usize; }
    }
    return count;
}

protected std.string::string body_text(const std.http::response* answer) throws std.error::fault {
    return std.string::from_utf8(answer->body.as_slice());
}

/* The requests of the demonstration: HTTP, HTTPS, the Unix socket, health, metrics and spans;
   then SIGTERM to the program, which stops the service with a drain. */
protected async void conversation(u16 http_port, u16 tls_port, std.string::string socket, std.http::client owner,
                                  std.service::health status)
    throws std.error::fault, std.http::http_error, std.tls::tls_error {
    std.http::client web = move owner;
    std.string::string hello_url = f"http://localhost:{http_port}/hello";
    std.string::string item_url = f"https://localhost:{tls_port}/items/7";
    std.string::string health_url = f"http://localhost:{http_port}/health";
    std.string::string metrics_url = f"http://localhost:{http_port}/metrics";
    std.string::string traces_url = f"http://localhost:{http_port}/traces";
    task_scope(1) first {
        std.http::response hello = await web.get(hello_url);
        u16 hello_status = hello.status;
        std.string::string hello_body = body_text(&hello);
        await std.console::print(f"http GET /hello -> {hello_status} {hello_body}");
    }
    task_scope(1) second {
        std.http::response item = await web.get(item_url);
        u16 item_status = item.status;
        std.string::string item_body = body_text(&item);
        await std.console::println(f"https GET /items/7 -> {item_status} {item_body}");
    }
    std.string::string unix_line = await unix_get(std.string::from_str(socket), std.string::from_str("/hello"));
    await std.console::println(f"unix GET /hello -> {unix_line}");
    task_scope(1) third {
        std.http::response health = await web.get(health_url);
        u16 health_status = health.status;
        std.string::string health_body = body_text(&health);
        await std.console::print(f"http GET /health -> {health_status} {health_body}");
    }
    task_scope(1) fourth {
        std.http::response metrics = await web.get(metrics_url);
        bool exposition = false;
        switch (metrics.headers.get("Content-Type")) {
        case variant o::some(kind): exposition = std.bytes::equal(*kind, std.metrics::content_type);
        case variant o::none: break;
        }
        await std.console::println(f"metrics in the exposition format: {exposition}");
        std.string::string metrics_body = body_text(&metrics);
        await std.console::print(lines_with(metrics_body, "telemetry_requests_total"));
        await std.console::print(lines_with(metrics_body, "telemetry_request_seconds_count"));
    }
    task_scope(1) fifth {
        std.http::response spans = await web.get(traces_url);
        usize span_count = count_lines(spans.body.as_slice());
        await std.console::println(f"spans: {span_count}");
    }
    bool serving = status.serving();
    u64 accepted = status.accepted();
    await std.console::println(f"serving: {serving}, accepted so far: {accepted}");
    std.signal::raise(std.signal::kind::terminate);
}

/* demo CA CERT KEY SOCKET: the service and a client of it in one program. */
protected async i32 demo(std.string::string ca, std.string::string cert, std.string::string key,
                         std.string::string socket)
    throws std.error::fault, std.tls::tls_error, std.http::http_error, std.metrics::metrics_error {
    bytes authority = await read_pem(move ca);
    std.tls::config client_tls = std.tls::client_config();
    client_tls.add_authority(authority.as_slice());
    arc std.tls::config verifying = new arc std.tls::config(move client_tls);
    std.tls::config tls = await server_tls(move cert, move key);
    arc std.tls::config shared_tls = new arc std.tls::config(move tls);
    std.net::tcp_listener plain = await listen_on(0u16);
    std.net::tcp_listener secure = await listen_on(0u16);
    u16 http_port = (plain.local_address()).port;
    u16 tls_port = (secure.local_address()).port;
    std.net::unix_listener local = await std.net::unix_listen(socket, 16u32, true, o::none);
    array<std.service::listener> listeners = [];
    add_listener(&listeners, std.service::listener::tcp(move plain));
    add_listener(&listeners, std.service::listener::tls {.socket = move secure, .settings = move shared_tls});
    add_listener(&listeners, std.service::listener::unix(move local));
    std.metrics::registry registry = std.metrics::registry::create();
    std.trace::tracer tracer = std.trace::tracer::create(256usize);
    arc example.telemetry.service::Telemetry state =
        new arc example.telemetry.service::Telemetry(example.telemetry.service::create(&registry, move tracer));
    std.service::health status = std.service::health::create();
    std.http::router<example.telemetry.service::Telemetry> table = routes(&status, &registry);
    std.sync::channel<std.service::stop> channel = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&channel);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move channel);
    std.http::client_options client_settings = {};
    std.http::limits bounds = {};
    task_scope(2) group {
        auto server = std.http::serve_all(move listeners, settings(), move stop, status.share(), move state,
                                          move table, bounds);
        auto talk = conversation(http_port, tls_port, move socket,
                                 std.http::client::with_tls(client_settings, move verifying), status.share());
        await move talk;
        std.service::report account = await move server;
        await std.console::println(summary(account));
    }
    bool after = status.serving();
    await std.console::println(f"serving after the signal: {after}");
    drop stopper;
    return 0;
}

/* The state of the probe service, which counts nothing. */
struct Quiet { u8 unused; };

/* Answers each connection of the probe with the index of its listener. */
protected async void answer_source(arc Quiet state, std.service::connection connection) throws std.error::fault {
    drop state;
    u32 origin = connection.source();
    std.string::string line = f"listener {origin}\n";
    task_scope(1) io { await connection.write_all_from(line); }
}

protected async std.string::string probe_client(std.string::string socket,
                                                std.sync::sender<std.service::stop> stopper)
    throws std.error::fault {
    std.net::unix_stream stream = await std.net::unix_connect(socket, o::none);
    bytes received = {};
    u8[64] buffer = {};
    bool open = true;
    while (open == true) {
        task_scope(1) reading {
            usize count = await std.net::unix_read_into(&stream, buffer[..], o::none);
            if (count == 0usize) { open = false; }
            std.bytes::append(&received, buffer[0usize..count]);
        }
    }
    await std.net::unix_close(move stream, o::none);
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
    return std.string::from_utf8(received.as_slice());
}

/* probe SOCKET: a plain service over a Unix-domain socket with std.service::serve_all, asked
   once by a client of the same program, which then stops it through its stop channel. */
protected async i32 probe(std.string::string socket) throws std.error::fault {
    std.net::unix_listener local = await std.net::unix_listen(socket, 4u32, true, o::none);
    array<std.service::listener> listeners = [];
    add_listener(&listeners, std.service::listener::unix(move local));
    std.sync::channel<std.service::stop> channel = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&channel);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move channel);
    u32 limit = std.service::max_listeners;
    task_scope(2) group {
        auto server = std.service::serve_all(move listeners, std.service::options {}, move stop,
                                             std.service::health::create(), new arc Quiet {.unused = 0u8},
                                             answer_source);
        auto client = probe_client(move socket, move stopper);
        std.string::string line = await move client;
        std.service::report account = await move server;
        u64 accepted = account.accepted;
        await std.console::print(f"{line}probe: accepted {accepted} of up to {limit} listeners\n");
    }
    return 0;
}

protected str usage_text() {
    return "telemetry serve HTTP_PORT TLS_PORT SOCKET CERT KEY\ntelemetry demo CA CERT KEY SOCKET\ntelemetry probe SOCKET\n";
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    str command = "";
    if (given >= 2usize) { command = arguments[1]; }
    try {
        if (std.bytes::equal(command, "serve") == true && given == 7usize) {
            u16 http_port = std.convert::parse_u16(arguments[2], 10u32);
            u16 tls_port = std.convert::parse_u16(arguments[3], 10u32);
            std.string::string socket = std.string::from_str(arguments[4]);
            std.string::string cert = std.string::from_str(arguments[5]);
            std.string::string key = std.string::from_str(arguments[6]);
            return await serve(http_port, tls_port, move socket, move cert, move key);
        }
        if (std.bytes::equal(command, "probe") == true && given == 3usize) {
            std.string::string path = std.string::from_str(arguments[2]);
            return await probe(move path);
        }
        if (std.bytes::equal(command, "demo") == true && given == 6usize) {
            std.string::string ca = std.string::from_str(arguments[2]);
            std.string::string cert = std.string::from_str(arguments[3]);
            std.string::string key = std.string::from_str(arguments[4]);
            std.string::string socket = std.string::from_str(arguments[5]);
            return await demo(move ca, move cert, move key, move socket);
        }
    } catch (std.convert::parse_error failure) {
        failure as void;
    } catch (std.tls::tls_error failure) {
        failure as void;
        await std.console::eprintln(f"tls configuration refused");
        return 66;
    } catch (std.http::http_error failure) {
        failure as void;
        return 70;
    } catch (std.metrics::metrics_error failure) {
        std.metrics::error_code code = failure.code;
        await std.console::eprintln(f"metric refused: {code}");
        return 70;
    } catch (std.error::fault failure) {
        std.string::string text = std.error::diagnostic(std.error::from_fault(failure));
        await std.console::eprintln(f"telemetry: {text}");
        return 71;
    }
    await std.console::eprint(std.string::from_str(usage_text()));
    if (given == 1usize) { return 0; }
    return 64;
}
