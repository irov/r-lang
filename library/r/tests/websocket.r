module tests.std.websocket;
import std.test;
import std.bufio;
import std.http;
import std.service;
import std.tls;
import std.websocket;

// The tests of std.websocket (Library R-SLIB-WS-0001..0005): the accept value and the frame
// form, a chat of an std.http server and a client over loopback TCP with text, large binary
// messages, pings and the closing handshake, a receiver and a sender in two tasks, the checks of
// the server against a raw client (fragments, a ping between them, invalid UTF-8, unmasked
// frames) and a refused handshake. Run in test mode (Core R-FUNC-0025).

struct Nothing { u32 unused; };

/* Answers each text message with "echo: " and the text and each binary message with its data,
   until the close of the peer. */
@scoped
protected async void echo_messages(const std.websocket::websocket* socket) throws std.error::fault {
    try {
        while (true) {
            o<std.websocket::message> next = o::none;
            task_scope(1) io {
                o<std.websocket::message> got = await socket->receive();
                o<std.websocket::message> old = core::replace(&next, move got);
                drop old;
            }
            switch (move next) {
            case variant o::none: return;
            case variant o::some(move item):
                switch (item.kind) {
                case std.websocket::message_kind::text:
                    std.string::string reply = std.string::from_str("echo: ");
                    std.string::append_str(&reply, item.text());
                    task_scope(1) io { await socket->send_text(reply); }
                case std.websocket::message_kind::binary:
                    task_scope(1) io { await socket->send_binary(item.data.as_slice()); }
                case std.websocket::message_kind::close: return;
                }
            }
        }
    } catch (std.websocket::websocket_error rejected) {
        // A peer that breaks the protocol has been answered with a close.
        rejected as void;
    }
}

protected async void chat(arc Nothing state, std.http::request incoming, std.http::upgrade connection)
    throws std.error::fault {
    drop state;
    o<std.websocket::websocket> opened = o::none;
    try {
        task_scope(1) handshake {
            std.websocket::websocket made = await std.websocket::accept(move incoming, move connection, "chat");
            o<std.websocket::websocket> old = core::replace(&opened, o::some(move made));
            drop old;
        }
    } catch (std.websocket::websocket_error rejected) {
        rejected as void;
    }
    switch (move opened) {
    case variant o::some(move socket): task_scope(1) io { await echo_messages(&socket); }
    case variant o::none: break;
    }
}

protected std.http::router<Nothing> chat_routes() throws std.http::http_error, std.alloc::alloc_error {
    std.http::router<Nothing> result = std.http::router<Nothing>::create();
    result.add_upgrade(std.http::method::get, "/chat", chat);
    return move result;
}

protected async std.net::tcp_listener open_listener() throws std.error::fault {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 16u32, .reuse_address = true, .v6_only = false};
    return await local.listen(options);
}

protected std.string::string chat_url(std.net::socket_address endpoint) throws std.alloc::alloc_error {
    u16 port = endpoint.port;
    return f"ws://127.0.0.1:{port}/chat";
}

protected void stop_server(std.sync::sender<std.service::stop> stopper) {
    std.sync::send_result<std.service::stop> sent = std.sync::send(&stopper, std.service::stop::drain);
    drop sent;
}

/* Opens a client WebSocket to the URL with the subprotocol. */
@scoped
protected async std.websocket::websocket open_client(std.http::client* web, str address, str protocol)
    throws std.error::fault, std.websocket::websocket_error, std.http::http_error, std.tls::tls_error {
    o<std.websocket::websocket> opened = o::none;
    task_scope(1) io {
        std.websocket::websocket made = await std.websocket::connect(web, address, protocol);
        o<std.websocket::websocket> old = core::replace(&opened, o::some(move made));
        drop old;
    }
    switch (move opened) {
    case variant o::some(move socket): return move socket;
    case variant o::none: throw std.websocket::websocket_error {.code = std.websocket::error_code::closed};
    }
}

/* The next message, which shall exist. */
@scoped
protected async std.websocket::message next_message(const std.websocket::websocket* socket)
    throws std.error::fault, std.websocket::websocket_error, std.test::failure {
    o<std.websocket::message> next = o::none;
    task_scope(1) io {
        o<std.websocket::message> got = await socket->receive();
        o<std.websocket::message> old = core::replace(&next, move got);
        drop old;
    }
    switch (move next) {
    case variant o::some(move item): return move item;
    case variant o::none: std.test::fail("a message");
    }
    throw std.websocket::websocket_error {.code = std.websocket::error_code::closed};
}

@scoped
protected async u32 talk(std.net::socket_address endpoint, std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.test::failure, std.websocket::websocket_error, std.http::http_error,
           std.tls::tls_error {
    std.http::client_options settings = {};
    std.http::client web = std.http::client::create(settings);
    std.string::string address = chat_url(endpoint);
    u32 checked = 0u32;
    bytes large = std.alloc::bytes(70000usize, 7u8);
    large[69999usize] = 9u8;
    str probe_text = "are you there";
    const u8[] probe = probe_text;
    task_scope(1) connect {
        std.websocket::websocket opened = await open_client(&web, address, "chat");
        task_scope(1) io {
            await opened.send_text("hello");
            std.websocket::message first = await next_message(&opened);
            std.test::check(first.kind == std.websocket::message_kind::text, "a text message");
            std.test::equal_text(first.text(), "echo: hello");
            await opened.send_binary(large.as_slice());
            std.websocket::message second = await next_message(&opened);
            std.test::check(second.kind == std.websocket::message_kind::binary, "a binary message");
            std.test::equal(len(second.data), 70000usize);
            std.test::equal(second.data[69999usize], 9u8);
            await opened.ping(probe);
            await opened.send_text("after the ping");
            std.websocket::message third = await next_message(&opened);
            std.test::equal_text(third.text(), "echo: after the ping");
            await opened.close(1000u16, "bye");
            std.websocket::message last = await next_message(&opened);
            std.test::check(last.kind == std.websocket::message_kind::close, "the close of the peer");
            std.test::equal(last.code, 1000u16);
            o<std.websocket::message> after = await opened.receive();
            switch (move after) {
            case variant o::some(move unexpected):
                drop unexpected;
                std.test::fail("nothing after the close");
            case variant o::none: checked += 1u32;
            }
        }
    }
    stop_server(move stopper);
    return checked;
}

@test
void computes_the_accept_key() throws std.test::failure, std.alloc::alloc_error {
    std.string::string value = std.websocket::accept_key("dGhlIHNhbXBsZSBub25jZQ==");
    std.test::equal_text(value, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

@test
void encodes_frames() throws std.test::failure, std.alloc::alloc_error {
    bytes small = std.websocket::encode_frame(1u8, true, "hi", false);
    std.test::equal(len(small), 4usize);
    std.test::equal(small[0usize], 129u8);
    std.test::equal(small[1usize], 2u8);
    bytes medium_payload = std.alloc::bytes(200usize, 1u8);
    bytes medium = std.websocket::encode_frame(2u8, false, medium_payload.as_slice(), false);
    std.test::equal(medium[0usize], 2u8);
    std.test::equal(medium[1usize], 126u8);
    std.test::equal(medium[2usize], 0u8);
    std.test::equal(medium[3usize], 200u8);
    std.test::equal(len(medium), 204usize);
    bytes large_payload = std.alloc::bytes(70000usize, 1u8);
    bytes large = std.websocket::encode_frame(2u8, true, large_payload.as_slice(), false);
    std.test::equal(large[1usize], 127u8);
    std.test::equal(large[7usize], 1u8);
    std.test::equal(large[8usize], 17u8);
    std.test::equal(large[9usize], 112u8);
    bytes masked = std.websocket::encode_frame(1u8, true, "abc", true);
    std.test::equal(masked[1usize], 131u8);
    std.test::equal(len(masked), 9usize);
    u32 restored = (masked[6usize] as u32) ^ (masked[2usize] as u32);
    std.test::equal(restored, 97u32);
}

@test
async void chats_over_http() throws std.error::fault, std.test::failure, std.websocket::websocket_error,
                                   std.http::http_error, std.tls::tls_error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Nothing state = new arc Nothing {.unused = 0u32};
    std.service::options settings = {};
    std.http::limits bounds = {};
    task_scope(2) group {
        auto server = std.http::serve(move listener, settings, move stop, move state, chat_routes(), bounds);
        u32 checked = await talk(endpoint, move stopper);
        std.service::report account = await move server;
        std.test::equal(checked, 1u32);
        std.test::equal(account.failed, 0u64);
    }
}

/* Sends count numbered messages. */
@scoped
protected async u32 send_many(const std.websocket::websocket* socket, u32 count)
    throws std.error::fault, std.websocket::websocket_error {
    for (u32 index = 0u32; index < count; index += 1u32) {
        std.string::string text = f"message {index}";
        task_scope(1) io { await socket->send_text(text); }
    }
    return count;
}

/* Receives count echoes, which arrive in order. */
@scoped
protected async u32 receive_many(const std.websocket::websocket* socket, u32 count)
    throws std.error::fault, std.websocket::websocket_error, std.test::failure {
    u32 matched = 0u32;
    for (u32 index = 0u32; index < count; index += 1u32) {
        std.string::string expected = f"echo: message {index}";
        task_scope(1) io {
            std.websocket::message item = await next_message(socket);
            if (std.bytes::equal(item.data.as_slice(), expected) == true) { matched += 1u32; }
        }
    }
    return matched;
}

/* One frame from the server, whose payload is at most 125 bytes. */
protected struct raw_frame { u8 opcode; bytes payload; };

@scoped
protected async raw_frame read_raw(std.bufio::reader<std.net::tcp_stream>* input)
    throws std.error::fault, std.test::failure {
    u8[2] head = {};
    u8[125] body = {};
    bytes payload = {};
    task_scope(1) io {
        bool got = await input->read_exact(&head);
        std.test::check(got, "a frame");
        usize size = ((head[1usize] as u32) & 127u32) as usize;
        if (size > 0usize) {
            bool rest = await input->read_exact(body[0usize..size]);
            std.test::check(rest, "the payload of the frame");
        }
        std.bytes::append(&payload, body[0usize..size]);
    }
    return raw_frame {.opcode = ((head[0usize] as u32) & 15u32) as u8, .payload = move payload};
}

/* The status code of a close frame. */
protected u32 close_status(const raw_frame* item) {
    if (len(item->payload) < 2usize) { return 0u32; }
    return ((item->payload[0usize] as u32) << 8u32) | (item->payload[1usize] as u32);
}

/* Performs the handshake by hand, then sends a fragmented text with a ping between its
   fragments and a text that breaks the protocol: invalid UTF-8, or an unmasked frame. Returns the
   status code of the close frame the server answers with. */
@scoped
protected async u32 raw_session(std.net::socket_address endpoint, bool unmasked)
    throws std.error::fault, std.test::failure {
    std.net::tcp_stream tcp = await endpoint.connect();
    std.bufio::reader<std.net::tcp_stream> input =
        std.bufio::reader<std.net::tcp_stream>::create(move tcp, 4096usize);
    str request_text =
        "GET /chat HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n"
        "Sec-WebSocket-Protocol: chat\r\n\r\n";
    const u8[] request_bytes = request_text;
    std.string::string line = std.string::create();
    bool switched = false;
    bool accepted = false;
    task_scope(1) handshake {
        await input.source.write_all_from(request_bytes);
        while (true) {
            bool got = await input.read_line(&line);
            std.test::check(got, "the head of the answer");
            if (std.string::len(&line) == 0usize) { break; }
            if (std.bytes::equal(line, "HTTP/1.1 101 Switching Protocols") == true) { switched = true; }
            if (std.bytes::equal(line, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == true) {
                accepted = true;
            }
        }
    }
    std.test::check(switched, "101 Switching Protocols");
    std.test::check(accepted, "the accept value of RFC 6455");
    bytes frames = std.websocket::encode_frame(1u8, false, "Hel", true);
    bytes pinging = std.websocket::encode_frame(9u8, true, "p", true);
    bytes rest = std.websocket::encode_frame(0u8, true, "lo", true);
    std.bytes::append(&frames, pinging.as_slice());
    std.bytes::append(&frames, rest.as_slice());
    task_scope(1) chat { await input.source.write_all_from(frames.as_slice()); }
    bytes broken = {};
    task_scope(1) replies {
        raw_frame pong = await read_raw(&input);
        std.test::equal(pong.opcode, 10u8);
        std.test::check(std.bytes::equal(pong.payload.as_slice(), "p"), "the pong carries the ping");
        raw_frame echo = await read_raw(&input);
        std.test::equal(echo.opcode, 1u8);
        std.test::check(std.bytes::equal(echo.payload.as_slice(), "echo: Hello"), "the joined fragments");
        if (unmasked == true) {
            bytes plain = std.websocket::encode_frame(1u8, true, "plain", false);
            std.bytes::append(&broken, plain.as_slice());
        } else {
            u8[3] invalid = {104u8, 255u8, 105u8};
            bytes wrong = std.websocket::encode_frame(1u8, true, &invalid, true);
            std.bytes::append(&broken, wrong.as_slice());
        }
        await input.source.write_all_from(broken.as_slice());
        raw_frame closing = await read_raw(&input);
        std.test::equal(closing.opcode, 8u8);
        return close_status(&closing);
    }
}

@scoped
protected async u32 protocol_checks(std.net::socket_address endpoint, std.sync::sender<std.service::stop> stopper)
    throws std.error::fault, std.test::failure, std.websocket::websocket_error, std.http::http_error,
           std.tls::tls_error {
    std.http::client_options settings = {};
    std.http::client web = std.http::client::create(settings);
    std.string::string address = chat_url(endpoint);
    u32 checked = 0u32;
    task_scope(1) connect {
        std.websocket::websocket opened = await open_client(&web, address, "chat");
        task_scope(2) both {
            auto sender = send_many(&opened, 20u32);
            u32 matched = await receive_many(&opened, 20u32);
            u32 sent = await move sender;
            std.test::equal(sent, 20u32);
            std.test::equal(matched, 20u32);
            checked += 1u32;
        }
        task_scope(1) done { await opened.close(1001u16, "going away"); }
    }
    task_scope(1) sessions {
        u32 invalid_text = await raw_session(endpoint, false);
        u32 unmasked = await raw_session(endpoint, true);
        std.test::equal(invalid_text, 1007u32);
        std.test::equal(unmasked, 1002u32);
        checked += 1u32;
    }
    try {
        task_scope(1) refused {
            std.websocket::websocket unexpected = await open_client(&web, address, "");
            drop unexpected;
        }
        std.test::fail("a handshake without the subprotocol is refused");
    } catch (std.websocket::websocket_error rejected) {
        std.test::check(rejected.code == std.websocket::error_code::handshake_failed, "handshake_failed");
        checked += 1u32;
    }
    stop_server(move stopper);
    return checked;
}

@test
async void checks_the_protocol_of_the_peer()
    throws std.error::fault, std.test::failure, std.websocket::websocket_error, std.http::http_error,
           std.tls::tls_error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.sync::channel<std.service::stop> factory = std.sync::channel::<std.service::stop>();
    std.sync::sender<std.service::stop> stopper = std.sync::sender(&factory);
    std.sync::receiver<std.service::stop> stop = std.sync::receiver(move factory);
    arc Nothing state = new arc Nothing {.unused = 0u32};
    std.service::options settings = {};
    std.http::limits bounds = {};
    task_scope(2) group {
        auto server = std.http::serve(move listener, settings, move stop, move state, chat_routes(), bounds);
        u32 checked = await protocol_checks(endpoint, move stopper);
        std.service::report account = await move server;
        std.test::equal(checked, 3u32);
        std.test::equal(account.failed, 0u64);
    }
}
