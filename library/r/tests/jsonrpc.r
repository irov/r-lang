module tests.std.jsonrpc;
import std.test;
import std.bufio;
import std.cmp;
import std.jsonrpc;

// The tests of std.jsonrpc (Library R-SLIB-JSONRPC-0001..0004): messages of every kind read from
// their text and written back, the parse and invalid-request failures with the id when it can be
// read, and newline-delimited framing over a loopback TCP connection. Run in test mode
// (Core R-FUNC-0025).

protected std.string::string round_trip(str text)
    throws std.test::failure, std.jsonrpc::rpc_error, std.json::error, std.alloc::alloc_error {
    std.jsonrpc::message parsed = std.jsonrpc::parse(text);
    return std.jsonrpc::encode(&parsed);
}

protected void same(str text)
    throws std.test::failure, std.jsonrpc::rpc_error, std.json::error, std.alloc::alloc_error {
    std.string::string written = round_trip(text);
    std.test::equal_text(written.as_str(), text);
}

/* The code of the failure that parsing the text throws, and whether it names an id. */
protected std.jsonrpc::rpc_error rejection(str text) throws std.test::failure, std.alloc::alloc_error {
    try {
        std.jsonrpc::message parsed = std.jsonrpc::parse(text);
        drop parsed;
    } catch (std.jsonrpc::rpc_error failure) {
        return move failure;
    }
    std.test::fail("the text is not a message");
    return std.jsonrpc::rpc_error {.code = 0i64, .message = std.string::create(), .id = o::none};
}

protected str id_text(const std.jsonrpc::rpc_error* failure) {
    switch (failure->id) {
    case variant o::some(value): return value->spelling();
    case variant o::none: return "none";
    }
}

@test
void reads_and_writes_every_kind() throws std.test::failure, std.jsonrpc::rpc_error, std.json::error,
                                          std.alloc::alloc_error {
    same("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":{\"name\":\"echo\"}}");
    same("{\"jsonrpc\":\"2.0\",\"id\":\"a-1\",\"method\":\"server/discover\"}");
    same("{\"jsonrpc\":\"2.0\",\"id\":123456789012345678901234567890,\"method\":\"x\",\"params\":[1,2]}");
    same("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/cancelled\",\"params\":{\"requestId\":7}}");
    same("{\"jsonrpc\":\"2.0\",\"id\":-5,\"result\":{\"resultType\":\"complete\"}}");
    same("{\"jsonrpc\":\"2.0\",\"id\":\"q\",\"result\":null}");
    same("{\"jsonrpc\":\"2.0\",\"id\":2,\"error\":{\"code\":-32601,\"message\":\"Method not found\"}}");
    same("{\"jsonrpc\":\"2.0\",\"error\":{\"code\":-32700,\"message\":\"Parse error\",\"data\":{\"at\":3}}}");
    std.string::string reordered =
        round_trip("{\"method\":\"m\",\"params\":{},\"id\":\"1\",\"jsonrpc\":\"2.0\",\"extra\":true}");
    std.test::equal_text(reordered.as_str(), "{\"jsonrpc\":\"2.0\",\"id\":\"1\",\"method\":\"m\",\"params\":{}}");
    std.jsonrpc::message request = std.jsonrpc::parse("{\"jsonrpc\":\"2.0\",\"id\":\"7\",\"method\":\"m\"}");
    switch (move request) {
    case variant std.jsonrpc::message::request(move item):
        std.test::check(item.id.is_text(), "a string id");
        std.test::equal_text(item.id.spelling(), "7");
        std.jsonrpc::request_id same_text = std.jsonrpc::request_id::from_text("7");
        std.jsonrpc::request_id number = std.jsonrpc::request_id::from_integer(7i64);
        std.test::check(std.cmp::is_equal(&item.id, &same_text), "equal ids");
        std.test::check(std.cmp::is_equal(&item.id, &number) == false, "a string and a number differ");
    default: std.test::fail("a request");
    }
}

@test
void rejects_what_is_not_a_message() throws std.test::failure, std.alloc::alloc_error {
    std.jsonrpc::rpc_error broken = rejection("{\"jsonrpc\":\"2.0\",");
    std.test::equal(broken.code, std.jsonrpc::parse_error);
    std.test::equal_text(id_text(&broken), "none");
    std.jsonrpc::rpc_error batch = rejection("[{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"m\"}]");
    std.test::equal(batch.code, std.jsonrpc::invalid_request);
    std.test::equal_text(id_text(&batch), "none");
    std.jsonrpc::rpc_error version = rejection("{\"jsonrpc\":\"1.0\",\"id\":3,\"method\":\"m\"}");
    std.test::equal(version.code, std.jsonrpc::invalid_request);
    std.test::equal_text(id_text(&version), "3");
    std.jsonrpc::rpc_error missing = rejection("{\"id\":4,\"method\":\"m\"}");
    std.test::equal_text(id_text(&missing), "4");
    std.jsonrpc::rpc_error null_id = rejection("{\"jsonrpc\":\"2.0\",\"id\":null,\"method\":\"m\"}");
    std.test::equal(null_id.code, std.jsonrpc::invalid_request);
    std.test::equal_text(id_text(&null_id), "none");
    std.jsonrpc::rpc_error method = rejection("{\"jsonrpc\":\"2.0\",\"id\":\"x\",\"method\":5}");
    std.test::equal(method.code, std.jsonrpc::invalid_request);
    std.test::equal_text(id_text(&method), "x");
    std.jsonrpc::rpc_error params = rejection("{\"jsonrpc\":\"2.0\",\"id\":6,\"method\":\"m\",\"params\":\"p\"}");
    std.test::equal_text(id_text(&params), "6");
    std.jsonrpc::rpc_error both = rejection("{\"jsonrpc\":\"2.0\",\"id\":7,\"result\":1,\"error\":{}}");
    std.test::equal(both.code, std.jsonrpc::invalid_request);
    std.jsonrpc::rpc_error neither = rejection("{\"jsonrpc\":\"2.0\",\"id\":8}");
    std.test::equal(neither.code, std.jsonrpc::invalid_request);
    std.jsonrpc::rpc_error code = rejection("{\"jsonrpc\":\"2.0\",\"id\":9,\"error\":{\"message\":\"m\"}}");
    std.test::equal_text(id_text(&code), "9");
    std.jsonrpc::rpc_error fraction =
        rejection("{\"jsonrpc\":\"2.0\",\"id\":9,\"error\":{\"code\":1.5,\"message\":\"m\"}}");
    std.test::equal(fraction.code, std.jsonrpc::invalid_request);
    std.jsonrpc::rpc_error text = rejection("\"just text\"");
    std.test::equal(text.code, std.jsonrpc::invalid_request);
}

@test
void answers_a_failure() throws std.test::failure, std.jsonrpc::rpc_error, std.json::error,
                                std.alloc::alloc_error {
    std.jsonrpc::rpc_error failure = rejection("{\"jsonrpc\":\"2.0\",\"id\":\"r\",\"method\":[]}");
    std.jsonrpc::message answer = std.jsonrpc::message::failure(std.jsonrpc::error_response::of(&failure));
    std.string::string text = std.jsonrpc::encode(&answer);
    std.test::equal_text(text.as_str(),
                         "{\"jsonrpc\":\"2.0\",\"id\":\"r\",\"error\":{\"code\":-32600,\"message\":\"Invalid Request\"}}");
}

protected async std.net::tcp_listener open_listener() throws std.error::fault {
    std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                     .scope_id = 0u32};
    std.net::listen_options options = {.backlog = 4u32, .reuse_address = true, .v6_only = false};
    return await local.listen(options);
}

/* Reads every line that the peer writes until it ends the stream. */
@scoped
protected async u32 read_all(std.bufio::reader<std.net::tcp_connection>* input, array<std.string::string>* lines)
    throws std.error::fault {
    u32 count = 0u32;
    while (true) {
        o<bytes> next = o::none;
        task_scope(1) io {
            o<bytes> got = await std.jsonrpc::read_line(input);
            o<bytes> old = core::replace(&next, move got);
            drop old;
        }
        switch (move next) {
        case variant o::some(move line):
            std.string::string text = std.string::from_utf8(line.as_slice());
            try {
                lines->push(move text);
            } catch (std.array::push_error<std.string::string> rejected) {
                (move rejected) as void;
                throw std.alloc::alloc_error::out_of_memory;
            }
            count += 1u32;
        case variant o::none: return count;
        }
    }
    return count;
}

@test
async void frames_messages_by_line() throws std.error::fault, std.test::failure, std.jsonrpc::rpc_error,
                                           std.json::error {
    std.net::tcp_listener listener = await open_listener();
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect();
    std.net::tcp_connection accepted = await listener.accept();
    std.jsonrpc::message first = std.jsonrpc::parse("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"a\",\"params\":{\"text\":\"two\\nlines\"}}");
    std.jsonrpc::message second = std.jsonrpc::parse("{\"jsonrpc\":\"2.0\",\"method\":\"b\"}");
    bytes extra = {};
    std.bytes::append(&extra, "\r\n\n{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{}}\r\n");
    std.bufio::reader<std.net::tcp_connection> input =
        std.bufio::reader<std.net::tcp_connection>::create(move accepted, 4096usize);
    array<std.string::string> lines = std.array::create::<std.string::string>();
    task_scope(1) io {
        await std.jsonrpc::write_message(&client, &first);
        await std.jsonrpc::write_message(&client, &second);
        await std.net::tcp_write_all_from(&client, extra.as_slice());
        await std.net::tcp_shutdown(&client, std.net::shutdown_direction::write);
        u32 count = await read_all(&input, &lines);
        std.test::equal(count, 3u32);
    }
    std.test::equal_text(lines[0usize].as_str(),
                         "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"a\",\"params\":{\"text\":\"two\\nlines\"}}");
    std.test::equal_text(lines[1usize].as_str(), "{\"jsonrpc\":\"2.0\",\"method\":\"b\"}");
    std.jsonrpc::message third = std.jsonrpc::parse(lines[2usize].as_str());
    std.string::string written = std.jsonrpc::encode(&third);
    std.test::equal_text(written.as_str(), "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{}}");
    await (move client).close();
    await (move listener).close();
}
