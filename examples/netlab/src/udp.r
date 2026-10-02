module example.netlab.udp;
import example.netlab.address;

struct CommandStorage1 { std.string::string value; };

async std.string::string deliver(std.string::string text, usize capacity)
    throws std.net::address_error, std.net::net_error, std.async::start_error, std.alloc::alloc_error,
        std.time::time_error {
    deadline (example.netlab.address::five_seconds()) {
        std.net::socket_address local = example.netlab.address::loopback();
        std.net::udp_socket sender = await local.bind(false);
        std.net::udp_socket receiver = await local.bind(false);
        std.net::socket_address destination = receiver.local_address();
        std.net::socket_address origin = sender.local_address();
        bytes message = (move text).into_bytes();
        std.net::udp_send_result sent = await sender.send_to(destination, move message);
        switch (move sent) {
        case variant std.net::udp_send_result::sent(move returned): break;
        case variant std.net::udp_send_result::failed(move failure): throw failure.error;
        }
        bytes buffer = std.alloc::bytes(capacity, 0u8);
        std.net::udp_receive_result result = await receiver.receive_from(move buffer);
        CommandStorage1 state_report = {.value = std.string::create()};
        switch (move result) {
        case variant std.net::udp_receive_result::received(move part):
            const u8[] contents = std.array::as_slice(&part.buffer);
            u32 checksum = std.hash::crc32(contents[0usize..part.count]);
            bool matched = part.peer.port == origin.port;
            state_report.value = f"received={part.count} crc32={checksum} truncated={part.truncated} endpoints_match={matched}\n";
            break;
        case variant std.net::udp_receive_result::failed(move failure): throw failure.error;
        }
        await (move sender).close();
        await (move receiver).close();
        return core::replace(&state_report.value, std.string::create());
    }
}
