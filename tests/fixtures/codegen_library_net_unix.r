module test.codegen.library_net_unix;
import std.stream;
import std.bufio;

// R-SLIB-NET-0014..0017 (M22): Unix-domain listeners, streams, peer credentials and datagrams,
// the path and establishment failures, and a Unix-domain stream through std.stream.

struct Count { usize value; };
struct Flag { bool value; };
struct Status { i32 value; };
struct Messages { std.net::unix_message whole; std.net::unix_message part; };

// Retain values whose purpose here is lifetime coverage on early exits.
@generic<T> @noalloc @nonblocking
protected void observe(const T* value) { value as void; }

/* R-SLIB-STREAM-0002: a Unix-domain stream is a std.stream::Writer. */
@scoped
async void send_all(const dyn(std.stream::Writer)* sink, const u8[] source)
    throws std.error::fault {
    task_scope(1) io { await sink->write_all_from(source); }
}

async i32 streams() throws std.error::fault {
    std.net::unix_listener listener = await std.net::unix_listen("net_unix_fixture.sock", 4u32, true);
    std.net::unix_stream client = await std.net::unix_connect("net_unix_fixture.sock");
    std.net::unix_stream server = await listener.accept();
    std.net::peer_credentials seen = server.peer_credentials();
    std.net::peer_credentials mine = std.net::unix_peer_credentials(&client);
    if (seen.user_id != mine.user_id || seen.group_id != mine.group_id ||
        seen.process_id != mine.process_id || seen.process_id <= 0) { return 1; }
    std.string::string payload_text = std.string::from_str("unix stream payload");
    bytes payload = (move payload_text).into_bytes();
    bytes received = std.alloc::bytes(64usize, 0u8);
    observe(&received);
    Count total = {.value = 0usize};
    task_scope(2) exchange {
        const u8[] first = payload.as_slice();
        usize sent = await client.write_from(first);
        if (sent == 0usize) { return 2; }
        const u8[] rest = payload.as_slice();
        await std.net::unix_write_all_from(&client, rest[sent..len(rest)]);
        await client.shutdown(std.net::shutdown_direction::write);
        while (true) {
            u8[] window = received.as_slice_mut();
            usize count = await server.read_into(window[total.value..len(window)]);
            if (count == 0usize) { break; }
            total.value += count;
        }
    }
    if (total.value != len(payload)) { return 3; }
    const u8[] got = received.as_slice();
    if (std.bytes::equal(got[0usize..total.value], payload.as_slice()) == false) { return 4; }
    /* The reverse direction through the std.stream traits: a line read by std.bufio. */
    std.string::string reply_text = std.string::from_str("ok\n");
    bytes reply = (move reply_text).into_bytes();
    task_scope(1) back { await send_all(&server, reply.as_slice()); }
    await std.net::unix_shutdown(&server, std.net::shutdown_direction::both);
    std.bufio::reader<std.net::unix_stream> lines =
        std.bufio::reader<std.net::unix_stream>::create(move client, 16usize);
    std.string::string line = std.string::create();
    Flag read = {.value = false};
    task_scope(1) input { read.value = await lines.read_line(&line); }
    if (read.value == false) { return 5; }
    std.string::string expected_text = std.string::from_str("ok");
    bytes expected = (move expected_text).into_bytes();
    if (std.bytes::equal(line.as_bytes(), expected.as_slice()) == false) { return 6; }
    drop lines;
    await std.net::unix_close(move server);
    await (move listener).close();
    return 0;
}

async i32 failures() throws std.net::net_error, std.async::start_error {
    try {
        std.net::unix_stream missing = await std.net::unix_connect("net_unix_fixture.none");
        drop missing;
        return 11;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::address_not_available) { return 12; }
    }
    try {
        std.net::unix_stream empty = await std.net::unix_connect("");
        drop empty;
        return 13;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::invalid_address || failure.native_code != 0i64) {
            return 14;
        }
    }
    /* A path of 103 bytes is the longest one. */
    std.net::unix_listener longest = await std.net::unix_listen(
        "net_unix_fixture_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx.sock", 0u32, true);
    await (move longest).close();
    try {
        std.net::unix_listener overlong = await std.net::unix_listen(
            "net_unix_fixture_xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxy.sock", 0u32, true);
        drop overlong;
        return 15;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::invalid_address) { return 16; }
    }
    std.net::unix_listener first = await std.net::unix_listen("net_unix_fixture.busy", 0u32, true);
    observe(&first);
    try {
        std.net::unix_listener second = await std.net::unix_listen("net_unix_fixture.busy", 0u32, false);
        drop second;
        return 17;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::address_in_use) { return 18; }
    }
    try {
        std.net::unix_datagram wrong = await std.net::unix_datagram_connect("net_unix_fixture.busy");
        drop wrong;
        return 19;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::unsupported) { return 20; }
    }
    std.net::unix_listener replaced = await std.net::unix_listen("net_unix_fixture.busy", 0u32, true);
    await std.net::unix_listener_close(move first);
    await (move replaced).close();
    return 0;
}

async i32 datagrams() throws std.net::net_error, std.async::start_error, std.alloc::alloc_error {
    std.net::unix_datagram receiver = await std.net::unix_datagram_bind("net_unix_fixture.dgram", true);
    std.net::unix_datagram sender = await std.net::unix_datagram_connect("net_unix_fixture.dgram");
    std.string::string message_text = std.string::from_str("datagram");
    bytes message = (move message_text).into_bytes();
    bytes buffer = std.alloc::bytes(16usize, 0u8);
    bytes small = std.alloc::bytes(4usize, 0u8);
    Messages seen = {.whole = {.count = 0usize, .truncated = true},
                     .part = {.count = 0usize, .truncated = false}};
    task_scope(2) exchange {
        auto arrival = std.net::unix_receive_into(&receiver, buffer.as_slice_mut());
        await sender.send_from(message.as_slice());
        seen.whole = await move arrival;
        await std.net::unix_send_from(&sender, message.as_slice());
        seen.part = await receiver.receive_into(small.as_slice_mut());
    }
    if (seen.whole.count != len(message) || seen.whole.truncated == true) { return 21; }
    const u8[] got = buffer.as_slice();
    if (std.bytes::equal(got[0usize..seen.whole.count], message.as_slice()) == false) {
        return 22;
    }
    if (seen.part.count != 4usize || seen.part.truncated == false) { return 23; }
    Status status = {.value = 0};
    try {
        task_scope(1) unaddressed { await receiver.send_from(message.as_slice()); }
        status.value = 24;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::not_connected) { status.value = 25; }
    }
    await (move sender).close();
    await std.net::unix_datagram_close(move receiver);
    return status.value;
}

async i32 main() {
    try {
        i32 status = await streams();
        if (status == 0) { status = await failures(); }
        if (status == 0) { status = await datagrams(); }
        return status;
    } catch (std.error::fault failure) {
        failure as void;
        return 94;
    } catch (std.net::net_error failure) {
        i64 native = failure.native_code;
        native as void;
        return 90;
    } catch (std.async::start_error failure) {
        failure as void;
        return 92;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 93;
    }
}
