module example.netlab.handoff;
import example.netlab.address;
import example.netlab.tcp::{Response};

/* The exchange of example.netlab.tcp written with owned buffers: every operation takes its buffer
   and gives it back in a tagged result that the caller unpacks. */

struct Receipt { usize count; u32 checksum; bool accepted; o<std.net::net_error> failure; };

// An effect-free completion record lets the sender release this task on any error path. The
// task inherits the deadline of the block that starts it.
async Receipt receive(std.net::tcp_connection connection) {
    try {
        bytes message = {};
        // The end of the stream leaves the labeled loop from the switch (R-STMT-0004).
        reading: while (true) {
            bytes buffer = std.alloc::bytes(4096usize, 0u8);
            std.net::tcp_read_result result = await std.net::tcp_read(&connection.stream, move buffer);
            switch (move result) {
            case variant std.net::tcp_read_result::read(move part):
                const u8[] source = std.array::as_slice(&part.buffer);
                for (usize index = 0usize; index < part.count; index += 1usize) {
                    std.bytes::append_u8(&message, source[index]);
                }
                break;
            case variant std.net::tcp_read_result::end(move returned): break reading;
            case variant std.net::tcp_read_result::failed(move failure): throw failure.error;
            }
        }
        usize length = len(message);
        if (length == 0usize) { return Receipt { .count = 0usize, .checksum = 0u32, .accepted = false, .failure = o::none }; }
        const u8[] contents = message.as_slice();
        bool version = contents[0] == 1u8;
        u32 checksum = std.hash::crc32(contents[1..length]);
        return Receipt { .count = length - 1usize, .checksum = checksum, .accepted = version, .failure = o::none };
    } catch (std.net::net_error failure) { return Receipt { .count = 0usize, .checksum = 0u32, .accepted = false, .failure = o::some(failure) }; }
    catch (std.async::start_error failure) { }
    catch (std.alloc::alloc_error failure) { }
    return Receipt { .count = 0usize, .checksum = 0u32, .accepted = false, .failure = o::none };
}

// Send a versioned message while another task receives it through a loopback TCP socket.
async Response deliver(std.string::string message)
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error {
    deadline (example.netlab.address::five_seconds()) {
        std.net::socket_address local = example.netlab.address::loopback();
        std.net::listen_options options = { .backlog = 4u32, .reuse_address = true, .v6_only = false };
        std.net::tcp_listener listener = await local.listen(options);
        std.net::socket_address endpoint = listener.local_address();
        std.net::tcp_stream client = await endpoint.connect();
        std.net::tcp_connection connection = await listener.accept();
        std.net::socket_address sender = client.local_address();
        std.net::socket_address peer = client.peer_address(); peer as void;
        bool matched = sender.port == connection.peer.port && peer.port == endpoint.port;
        await (move listener).close();
        task<Receipt> receiver = receive(move connection);
        bytes header = std.alloc::bytes(1usize, 1u8);
        std.net::tcp_write_result first = await client.write(move header);
        switch (move first) {
        case variant std.net::tcp_write_result::written(move part): break;
        case variant std.net::tcp_write_result::failed(move failure): throw failure.error;
        }
        bytes body = (move message).into_bytes();
        std.net::tcp_write_all_result sent = await client.write_all(move body);
        switch (move sent) {
        case variant std.net::tcp_write_all_result::written(move returned): break;
        case variant std.net::tcp_write_all_result::failed(move failure): throw failure.error;
        }
        await client.shutdown(std.net::shutdown_direction::write);
        Receipt receipt = await move receiver;
        await (move client).close();
        switch (receipt.failure) {
        case variant o::some(failure): throw *failure;
        case variant o::none: break;
        }
        if (receipt.accepted == false) {
            return Response {.output = std.string::from_str("receiver did not accept the message\n"), .status = 69};
        }
        std.string::string line = f"received={receipt.count} crc32={receipt.checksum} accepted={receipt.accepted} endpoints_match={matched}\n";
        return Response {.output = move line, .status = 0};
    }
}
