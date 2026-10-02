module test.codegen.scoped_net;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { usize value; };
struct TestStorage2 { std.net::datagram value; };

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


/* R-SLIB-NET-0010: scoped TCP and UDP operations loan the caller's buffers to the task group
   until the backend acknowledges each outcome; the consuming await releases each loan. */
std.net::socket_address loopback() throws std.net::address_error {
    return std.net::socket_address { .address = std.net::parse_ip("127.0.0.1"), .port = 0u16, .scope_id = 0u32 };
}

async i32 tcp_round_trip() throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error {
    std.net::socket_address local = loopback();
    std.net::listen_options options = { .backlog = 4u32, .reuse_address = true, .v6_only = false };
    std.net::tcp_listener listener = await local.listen(options, o::none);
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect(o::none);
    std.net::tcp_connection connection = await listener.accept(o::none);
    test_observe(&connection);
    await (move listener).close(o::none);
    std.string::string text = std.string::from_str("scoped stream payload");
    bytes payload = (move text).into_bytes();
    bytes received = std.alloc::bytes(64usize, 0u8);
    test_observe(&received);
    usize total = 0usize;
    TestStorage1 storage_sent = {.value = 0usize};
    task_scope(2) exchange {
        const u8[] first = payload.as_slice();
        storage_sent.value = await client.write_from(first, o::none);
        if (storage_sent.value == 0usize) { return 10; }
        const u8[] rest = payload.as_slice();
        await std.net::tcp_write_all_from(&client, rest[storage_sent.value..len(rest)], o::none);
        await client.shutdown(std.net::shutdown_direction::write, o::none);
        while (true) {
            u8[] window = received.as_slice_mut();
            usize count = await std.net::tcp_read_into(&connection.stream, window[total..len(window)], o::none);
            if (count == 0usize) { break; }
            total += count;
        }
    }
    await (move client).close(o::none);
    if (total != len(payload)) { return 11; }
    const u8[] got = received.as_slice();
    const u8[] expected = payload.as_slice();
    if (std.bytes::equal(got[0usize..total], expected) == false) { return 12; }
    return 0;
}

async i32 udp_round_trip() throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error {
    std.net::socket_address local = loopback();
    std.net::udp_socket sender = await local.bind(false, o::none);
    std.net::udp_socket receiver = await local.bind(false, o::none);
    std.net::socket_address destination = receiver.local_address();
    std.net::socket_address origin = sender.local_address();
    std.string::string text = std.string::from_str("datagram");
    bytes payload = (move text).into_bytes();
    bytes buffer = std.alloc::bytes(16usize, 0u8);
    TestStorage2 storage_received = {.value = std.net::datagram { .count = 0usize, .peer = origin, .truncated = false }};
    task_scope(2) exchange {
        auto arrival = std.net::udp_receive_into(&receiver, buffer.as_slice_mut(), o::none);
        const u8[] message = payload.as_slice();
        await sender.send_from(destination, message, o::none);
        storage_received.value = await move arrival;
    }
    await (move sender).close(o::none);
    await (move receiver).close(o::none);
    if (storage_received.value.count != len(payload) || storage_received.value.peer.port != origin.port || storage_received.value.truncated == true) { return 20; }
    const u8[] got = buffer.as_slice();
    const u8[] expected = payload.as_slice();
    if (std.bytes::equal(got[0usize..storage_received.value.count], expected) == false) { return 21; }
    return 0;
}

async i32 main() {
    try {
        try {
            i32 tcp = await tcp_round_trip();
            if (tcp != 0) { return tcp; }
            return await udp_round_trip();
        } catch (std.net::address_error failure) { throw TestAssertionFailed {.code = 65}; }
        catch (std.net::net_error failure) { throw TestAssertionFailed {.code = 69}; }
        catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 71}; }
        catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 75}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
