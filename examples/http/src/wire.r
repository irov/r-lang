module example.http.wire;
import std.console;
import std.bufio;
import std.http;

/* The server side: reads a request head, streams its body in pieces, answers in chunks. */
@scoped
protected async void answer(std.bufio::reader<std.net::tcp_connection>* input)
    throws std.error::fault, std.http::http_error {
    std.http::limits bounds = {};
    o<std.http::request> head = o::none;
    task_scope(1) io {
        o<std.http::request> read = await std.http::read_request_head(input, &bounds);
        switch (move read) {
        case variant o::some(move value): head = o::some(move value);
        case variant o::none: break;
        }
    }
    switch (move head) {
    case variant o::some(move incoming):
        std.http::body_state state = incoming.framing();
        u8[4] piece = {};
        usize first_read = 0usize;
        task_scope(1) io { first_read += await std.http::read_body(input, &state, &piece); }
        bytes rest = {};
        task_scope(1) io {
            bytes remaining = await std.http::read_whole_body(input, &state, 64usize);
            rest.append(remaining.as_slice());
        }
        usize total = first_read + len(rest);
        usize pieces = 2usize;
        str sent = std.http::method_name(incoming.method);
        std.http::response reply = std.http::response::create(200u16);
        reply.headers.add("Content-Type", "text/plain");
        std.string::string first = std.string::from_str("chunked ");
        std.string::string second = std.string::from_str("reply");
        task_scope(1) io {
            await std.console::println(
                f"server: {sent} {incoming.target} body {total} bytes in {pieces} reads");
            await std.http::write_chunked_head(&input->source, &reply, false);
            await std.http::write_chunk(&input->source, first);
            await std.http::write_chunk(&input->source, second);
            await std.http::finish_chunks(&input->source);
        }
    case variant o::none: break;
    }
    o<std.http::request> next = o::none;
    task_scope(1) io {
        o<std.http::request> read = await std.http::read_request(input, &bounds);
        switch (move read) {
        case variant o::some(move value): next = o::some(move value);
        case variant o::none: break;
        }
    }
    switch (move next) {
    case variant o::some(move incoming):
        std.http::response empty = std.http::response::create(204u16);
        str phrase = std.http::reason(204u16);
        usize size = len(incoming.body);
        task_scope(1) io {
            await std.console::println(f"server: second request body {size}, answering 204 {phrase}");
            await std.http::write_response(&input->source, &empty, false, true);
        }
    case variant o::none: break;
    }
}

/* The client side: a POST with a body, then a PUT, over one connection. */
@scoped
protected async void ask(std.bufio::reader<std.net::tcp_stream>* output)
    throws std.error::fault, std.http::http_error {
    std.http::limits bounds = {};
    std.http::method put = std.http::method::get;
    switch (std.http::parse_method("PUT")) {
    case variant o::some(value): put = *value;
    case variant o::none: break;
    }
    std.http::request upload = std.http::request::create(std.http::method::post, "/upload");
    std.bytes::append(&upload.body, "eleven byte");
    std.http::request second = std.http::request::create(put, "/state");
    std.bytes::append(&second.body, "on");
    task_scope(1) io {
        await std.http::write_request(&output->source, &upload, "localhost");
        std.http::response first = await std.http::read_response(output, upload.method, &bounds);
        std.string::string text = std.string::from_utf8(first.body.as_slice());
        std.http::header shown = {.name = std.string::from_str("Content-Type"),
                                  .value = std.string::from_str("?")};
        switch (first.headers.get("Content-Type")) {
        case variant o::some(kind):
            std.string::string old = core::replace(&shown.value, std.string::from_str(*kind));
            drop old;
        case variant o::none: break;
        }
        await std.console::println(f"client: {first.status} {shown.name}={shown.value} body {text}");
        await std.http::write_request(&output->source, &second, "localhost");
        std.http::response last = await std.http::read_response(output, second.method, &bounds);
        bool closing = last.headers.has_token("Connection", "close");
        await std.console::println(f"client: {last.status} close={closing}");
    }
}

/* A client and a server of the same process speak HTTP/1.1 over one loopback TCP connection
   with the message functions of std.http. */
async i32 wire() throws std.error::fault, std.http::http_error {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 1u32, .reuse_address = true, .v6_only = false};
    std.net::tcp_listener listener = await local.listen(options);
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect();
    std.net::tcp_connection server = await listener.accept();
    await (move listener).close();
    std.bufio::reader<std.net::tcp_connection> input =
        std.bufio::reader<std.net::tcp_connection>::create(move server, 4096usize);
    std.bufio::reader<std.net::tcp_stream> output =
        std.bufio::reader<std.net::tcp_stream>::create(move client, 4096usize);
    task_scope(2) pair {
        auto serving = answer(&input);
        auto asking = ask(&output);
        await move asking;
        await move serving;
    }
    return 0;
}
