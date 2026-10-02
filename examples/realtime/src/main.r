module example.realtime.main;
import std.console;
import std.dns;
import std.encoding;
import std.http;
import std.net;
import std.service;
import std.text;
import std.tls;
import std.websocket;
import example.realtime.line;
import example.realtime.names;
import example.realtime.room;

protected async std.net::tcp_listener listen_loopback() throws std.error::fault {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 8u32, .reuse_address = true, .v6_only = false};
    return await local.listen(options);
}

protected array<std.net::socket_address> one(std.net::socket_address server) throws std.alloc::alloc_error {
    array<std.net::socket_address> servers = std.array::create::<std.net::socket_address>();
    try {
        std.array::push(&servers, server);
    } catch (std.array::push_error<std.net::socket_address> rejected) {
        rejected as void;
        throw std.alloc::alloc_error::out_of_memory;
    }
    return move servers;
}

/* Where the chat service is: the host and port of its SRV record and the path of its TXT
   record. */
struct Service { std.string::string host; u16 port; std.string::string path; };

/* Finds the chat service with the name server of the example and prints what it found. */
@scoped
protected async Service discover(std.net::socket_address name_server)
    throws std.error::fault, std.dns::dns_error {
    std.dns::resolver resolving = std.dns::resolver::with_servers(one(name_server));
    Service found = {.host = std.string::create(), .port = 0u16, .path = std.string::from_str("/")};
    task_scope(1) service {
        array<std.dns::srv_record> services = await resolving.lookup_srv("_chat._tcp.realtime.test");
        throw (len(services) == 0usize) std.dns::dns_error {.code = std.dns::error_code::name_error};
        std.string::append_str(&found.host, services[0usize].target.as_str());
        found.port = services[0usize].port;
        str host = found.host.as_str();
        u16 priority = services[0usize].priority;
        u16 weight = services[0usize].weight;
        await std.console::println(f"srv _chat._tcp.realtime.test -> {host} priority {priority} weight {weight}");
    }
    task_scope(1) text {
        array<std.dns::txt_record> texts = await resolving.lookup_txt("_chat._tcp.realtime.test");
        for (usize index = 0usize; index < len(texts); index += 1usize) {
            std.string::string line = texts[index].text();
            if (std.text::starts_with(line.as_str(), "path=") == true) {
                const u8[] bytes_of = line.as_bytes();
                std.string::string path = std.string::create();
                std.string::append_utf8(&path, bytes_of[5usize..len(bytes_of)]);
                std.string::string old = core::replace(&found.path, move path);
                drop old;
            }
            await std.console::println(f"txt {line}");
        }
    }
    task_scope(1) reverse {
        array<std.string::string> names = await resolving.reverse(std.net::parse_ip("127.0.0.1"));
        for (usize index = 0usize; index < len(names); index += 1usize) {
            str name = names[index].as_str();
            await std.console::println(f"ptr 127.0.0.1 -> {name}");
        }
    }
    return move found;
}

/* Opens a WebSocket to the URL with the subprotocol chat. */
@scoped
protected async std.websocket::websocket open_socket(std.http::client* web, str address)
    throws std.error::fault, std.websocket::websocket_error, std.http::http_error, std.tls::tls_error {
    o<std.websocket::websocket> opened = o::none;
    task_scope(1) io {
        std.websocket::websocket made = await std.websocket::connect(web, address, "chat");
        o<std.websocket::websocket> old = core::replace(&opened, o::some(move made));
        drop old;
    }
    switch (move opened) {
    case variant o::some(move socket): return move socket;
    case variant o::none: throw std.websocket::websocket_error {.code = std.websocket::error_code::closed};
    }
}

/* Prints the next message of the member: "who <- text", or "who closed CODE". */
@scoped
protected async void show_next(const std.websocket::websocket* socket, str who)
    throws std.error::fault, std.websocket::websocket_error {
    o<std.websocket::message> next = o::none;
    task_scope(1) io {
        o<std.websocket::message> got = await socket->receive();
        o<std.websocket::message> old = core::replace(&next, move got);
        drop old;
    }
    switch (move next) {
    case variant o::some(move item):
        if (item.kind == std.websocket::message_kind::close) {
            u16 code = item.code;
            await std.console::println(f"{who} closed {code}");
            return;
        }
        str text = item.text();
        await std.console::println(f"{who} <- {text}");
    case variant o::none: await std.console::println(f"{who} ended");
    }
}

/* Alice and Bob join the room, say one line each and leave. */
@scoped
protected async void chat(std.http::client* web, const Service* found)
    throws std.error::fault, std.websocket::websocket_error, std.http::http_error, std.tls::tls_error {
    str host = found->host.as_str();
    u16 port = found->port;
    str path = found->path.as_str();
    std.string::string alice_url = f"ws://{host}:{port}{path}?name=alice";
    std.string::string bob_url = f"ws://{host}:{port}{path}?name=bob";
    task_scope(1) first {
        std.websocket::websocket alice = await open_socket(web, alice_url.as_str());
        task_scope(1) joined { await show_next(&alice, "alice"); }
        std.websocket::websocket bob = await open_socket(web, bob_url.as_str());
        task_scope(1) talk {
            await show_next(&alice, "alice");
            await show_next(&bob, "bob");
            await alice.send_text("hello");
            await show_next(&alice, "alice");
            await show_next(&bob, "bob");
            await bob.send_text("hi alice");
            await show_next(&alice, "alice");
            await show_next(&bob, "bob");
            await bob.close(1000u16, "bye");
            await show_next(&bob, "bob");
            await show_next(&alice, "alice");
            await alice.close(1000u16, "bye");
            await show_next(&alice, "alice");
        }
    }
}

/* The routes of the server: the chat room and the line protocol, both upgrade routes. */
protected std.http::router<example.realtime.room::Room> routes()
    throws std.http::http_error, std.alloc::alloc_error {
    std.http::router<example.realtime.room::Room> result =
        std.http::router<example.realtime.room::Room>::create();
    result.add_upgrade(std.http::method::get, "/chat", example.realtime.room::join);
    result.add_upgrade(std.http::method::get, "/line", example.realtime.line::line);
    return move result;
}

/* Switches to the line protocol on /line, and asks /chat for it, which refuses. */
@scoped
protected async void lines(std.http::client* web, const Service* found)
    throws std.error::fault, std.http::http_error, std.tls::tls_error {
    str host = found->host.as_str();
    u16 port = found->port;
    std.string::string line_url = f"http://{host}:{port}/line";
    std.string::string chat_url = f"http://{host}:{port}/chat";
    task_scope(1) io {
        std.string::string accepted = await example.realtime.line::ask(web, line_url.as_str(), "quiet words\n");
        await std.console::println(f"upgrade /line -> {accepted}");
        std.string::string refused = await example.realtime.line::ask(web, chat_url.as_str(), "quiet words\n");
        await std.console::println(f"upgrade /chat -> {refused}");
    }
}

@scoped
protected async void visit(std.net::socket_address name_server, std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.dns::dns_error, std.websocket::websocket_error, std.http::http_error,
           std.tls::tls_error {
    std.http::client_options settings = {};
    std.http::client web = std.http::client::create(settings);
    task_scope(1) io {
        Service found = await discover(name_server);
        task_scope(1) talk {
            await chat(&web, &found);
            await lines(&web, &found);
        }
    }
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
}

protected async i32 demo()
    throws std.error::fault, std.dns::dns_error, std.websocket::websocket_error, std.http::http_error,
           std.tls::tls_error {
    std.net::tcp_listener listener = await listen_loopback();
    std.net::socket_address endpoint = listener.local_address();
    std.net::udp_socket names = await example.realtime.names::open();
    std.net::socket_address name_server = names.local_address();
    std.sync::channel<std.service::stop> channel = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&channel);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move channel);
    arc example.realtime.room::Room state = new arc example.realtime.room::Room(
        example.realtime.room::create());
    std.service::options settings = {};
    std.http::limits bounds = {};
    task_scope(3) group {
        auto server = std.http::serve(move listener, settings, move stop, move state,
                                      routes(), bounds);
        auto zone = example.realtime.names::serve(&names, endpoint.port, 3u32);
        await visit(name_server, move stopper);
        await move zone;
        std.service::report report = await move server;
        u64 accepted = report.accepted;
        await std.console::println(f"connections: {accepted}");
    }
    await (move names).close();
    return 0;
}

/* Prints the accept value of a Sec-WebSocket-Key. */
protected async i32 accept(std.string::string key) throws std.error::fault {
    std.string::string value = std.websocket::accept_key(key.as_str());
    await std.console::println(move value);
    return 0;
}

/* Prints an unmasked final text frame of the text in hexadecimal. */
protected async i32 frame(std.string::string text) throws std.error::fault {
    bytes encoded = std.websocket::encode_frame(1u8, true, text.as_bytes(), false);
    await std.console::println(std.encoding::encode_hex(encoded.as_slice()));
    return 0;
}

/* The record type of a name, 0 for none. */
protected u16 type_of(str name) {
    if (std.bytes::equal(name, "a") == true) { return std.dns::type_code(std.dns::record_type::a); }
    if (std.bytes::equal(name, "aaaa") == true) { return std.dns::type_code(std.dns::record_type::aaaa); }
    if (std.bytes::equal(name, "srv") == true) { return std.dns::type_code(std.dns::record_type::srv); }
    if (std.bytes::equal(name, "txt") == true) { return std.dns::type_code(std.dns::record_type::txt); }
    if (std.bytes::equal(name, "ptr") == true) { return std.dns::type_code(std.dns::record_type::ptr); }
    return 0u16;
}

/* The record type the fourth argument names, 0 for none. */
protected u16 requested_type(const array<std.string::string>* arguments) {
    if (len(*arguments) != 4usize) { return 0u16; }
    return type_of((*arguments)[3usize].as_str());
}

/* Prints the query with id 4660 for the name and type in hexadecimal. */
protected async i32 query(std.string::string name, u16 kind) throws std.error::fault, std.dns::dns_error {
    bytes encoded = std.dns::encode_query(4660u16, name.as_str(), kind);
    await std.console::println(std.encoding::encode_hex(encoded.as_slice()));
    return 0;
}

/* Prints the reverse name and the octets of an address. */
protected async i32 reverse(std.string::string text) throws std.error::fault {
    try {
        std.net::ip_address address = std.net::parse_ip(text.as_str());
        std.string::string name = std.dns::reverse_name(address);
        bytes octets = std.net::ip_octets(address);
        std.string::string digits = std.encoding::encode_hex(octets.as_slice());
        await std.console::println(f"{name} {digits}");
        return 0;
    } catch (std.net::address_error rejected) {
        rejected as void;
    }
    await std.console::eprintln(std.string::from_str("address: invalid"));
    return 65;
}

/* Prints the name servers of a resolv.conf file. */
protected async i32 resolv(std.string::string file) throws std.error::fault {
    std.fs::path path = std.fs::path_from_utf8(file.as_str());
    bytes content = await path.read_file(65536usize);
    std.string::string text = std.string::from_str("");
    try {
        std.string::append_utf8(&text, content.as_slice());
    } catch (std.string::string_error rejected) {
        rejected as void;
    }
    array<std.net::socket_address> servers = std.dns::parse_resolv_conf(text.as_str());
    for (usize index = 0usize; index < len(servers); index += 1usize) {
        std.string::string shown = std.net::format_ip(servers[index].address);
        u16 port = servers[index].port;
        await std.console::println(f"nameserver {shown} port {port}");
    }
    return 0;
}

/* Answers an SRV question for the name with one record on the port and reads the answer back. */
protected async i32 answer(std.string::string name, u16 port) throws std.error::fault, std.dns::dns_error {
    bytes asking = std.dns::encode_query(7u16, name.as_str(), 33u16);
    std.dns::question asked = std.dns::parse_query(asking.as_slice());
    array<std.dns::record> records = std.array::create::<std.dns::record>();
    std.dns::record entry = std.dns::record::srv(asked.name.as_str(), 60u32, 10u16, 5u16, port, "localhost.");
    try {
        records.push(move entry);
    } catch (std.array::push_error<std.dns::record> rejected) {
        (move rejected) as void;
        return 70;
    }
    bytes response = std.dns::encode_answer(&asked, &records, 0u8, false);
    array<std.dns::record> found = std.dns::parse_response(response.as_slice(), 7u16, name.as_str(), 33u16);
    for (usize index = 0usize; index < len(found); index += 1usize) {
        str owner = found[index].name.as_str();
        u32 ttl = found[index].ttl;
        u16 priority = found[index].priority;
        u16 weight = found[index].weight;
        u16 target_port = found[index].port;
        str target = found[index].target.as_str();
        usize size = len(response);
        await std.console::println(
            f"{owner} ttl {ttl} srv {priority} {weight} {target_port} {target} ({size} bytes)");
    }
    return 0;
}

protected str usage_text() {
    return "realtime demo\nrealtime accept KEY\nrealtime frame TEXT\n"
           "realtime query NAME a|aaaa|srv|txt|ptr\nrealtime reverse ADDRESS\n"
           "realtime resolv FILE\nrealtime answer NAME PORT\n";
}

async i32 main() {
    array<std.string::string> arguments = std.env::arguments();
    usize given = len(arguments);
    str command = "";
    if (given >= 2usize) { command = arguments[1].as_str(); }
    u16 kind = requested_type(&arguments);
    bool demo_call = std.bytes::equal(command, "demo") == true && given == 2usize;
    bool accept_call = std.bytes::equal(command, "accept") == true && given == 3usize;
    bool frame_call = std.bytes::equal(command, "frame") == true && given == 3usize;
    bool query_call = kind != 0u16 && std.bytes::equal(command, "query") == true && given == 4usize;
    bool reverse_call = std.bytes::equal(command, "reverse") == true && given == 3usize;
    bool resolv_call = std.bytes::equal(command, "resolv") == true && given == 3usize;
    bool answer_call = std.bytes::equal(command, "answer") == true && given == 4usize;
    if (demo_call == false && accept_call == false && frame_call == false && query_call == false &&
        reverse_call == false && resolv_call == false && answer_call == false) {
        drop arguments;
        await std.console::eprint(std.string::from_str(usage_text()));
        if (given == 1usize) { return 0; }
        return 64;
    }
    try {
        if (accept_call == true) {
            std.string::string key = core::replace(&arguments[2], std.string::create());
            drop arguments;
            return await accept(move key);
        }
        if (frame_call == true) {
            std.string::string text = core::replace(&arguments[2], std.string::create());
            drop arguments;
            return await frame(move text);
        }
        if (query_call == true) {
            std.string::string name = core::replace(&arguments[2], std.string::create());
            drop arguments;
            return await query(move name, kind);
        }
        if (reverse_call == true) {
            std.string::string text = core::replace(&arguments[2], std.string::create());
            drop arguments;
            return await reverse(move text);
        }
        if (resolv_call == true) {
            std.string::string file = core::replace(&arguments[2], std.string::create());
            drop arguments;
            return await resolv(move file);
        }
        if (answer_call == true) {
            std.string::string name = core::replace(&arguments[2], std.string::create());
            u16 port = std.convert::parse_u16(arguments[3].as_str(), 10u32);
            drop arguments;
            return await answer(move name, port);
        }
        drop arguments;
        return await demo();
    } catch (std.convert::parse_error failure) {
        failure as void;
        await std.console::eprintln(std.string::from_str("port: invalid"));
    } catch (std.dns::dns_error failure) {
        await std.console::eprintln(f"dns: {failure.code}");
    } catch (std.websocket::websocket_error failure) {
        await std.console::eprintln(f"websocket: {failure.code}");
    } catch (std.http::http_error failure) {
        await std.console::eprintln(f"http: {failure.code}");
    } catch (std.tls::tls_error failure) {
        await std.console::eprintln(f"tls: {failure.code}");
    }
    return 65;
}
