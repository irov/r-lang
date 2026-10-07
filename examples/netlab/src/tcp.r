module example.netlab.tcp;
import example.netlab.address;

// The output and exit status of a command.
struct Response { std.string::string output; i32 status; };

// What the receiving task observed.
struct Receipt { usize count; u32 checksum; bool accepted; };

// The longest packet: a one-byte protocol version and a message of at most 1 MiB.
const usize LIMIT = 1048577usize;

/* Read until the sender ends its side. Every read is one line, and the deadline of the task
   that receives bounds it. */
@scoped
async Receipt receive(const std.net::tcp_stream* stream, u8[] buffer)
    throws std.net::net_error, std.async::start_error {
    usize length = 0usize;
    usize count = 1usize;
    task_scope(1) io {
        while (count != 0usize && length < len(buffer)) {
            count = await std.net::tcp_read_into(stream, buffer[length..len(buffer)]);
            length += count;
        }
    }
    if (length == 0usize) { return Receipt {.count = 0usize, .checksum = 0u32, .accepted = false}; }
    return Receipt {.count = length - 1usize, .checksum = std.hash::crc32(buffer[1usize..length]),
                    .accepted = buffer[0] == 1u8};
}

// The protocol version byte followed by the message.
bytes versioned(std.string::string message) throws std.alloc::alloc_error {
    bytes packet = std.alloc::bytes(1usize, 1u8);
    packet.append(message);
    return move packet;
}

Response report(Receipt receipt, bool matched) throws std.alloc::alloc_error {
    if (receipt.accepted == false) {
        return Response {.output = std.string::from_str("receiver did not accept the message\n"), .status = 69};
    }
    std.string::string line = f"received={receipt.count} crc32={receipt.checksum} accepted=true endpoints_match={matched}\n";
    return Response {.output = move line, .status = 0};
}

// Send a versioned message while another task receives it through a loopback TCP socket.
async Response deliver(std.string::string message) throws std.error::fault {
    deadline (example.netlab.address::five_seconds()) {
        std.net::tcp_listener listener = await example.netlab.address::listen();
        std.net::socket_address endpoint = listener.local_address();
        std.net::tcp_stream client = await endpoint.connect();
        std.net::tcp_connection connection = await listener.accept();
        bool matched = (client.local_address()).port == connection.peer.port &&
                       (client.peer_address()).port == endpoint.port;
        bytes packet = versioned(move message);
        bytes received = std.alloc::bytes(LIMIT, 0u8);
        task_scope(2) exchange {
            auto receiver = receive(&connection.stream, received.as_slice_mut());
            await std.net::tcp_write_all_from(&client, packet.as_slice());
            await client.shutdown(std.net::shutdown_direction::write);
            return report(await move receiver, matched);
        }
    }
}
