module example.netlab.tune;
import example.netlab.address;

/* The keepalive of a TCP option record: the idle seconds, or off. */
std.string::string keepalive_text(std.net::tcp_options options) throws std.alloc::alloc_error {
    switch (options.keepalive) {
    case variant o::some(idle):
        i64 seconds = std.time::duration_seconds(*idle);
        return f"{seconds}";
    case variant o::none: return std.string::from_str("off");
    }
}

/* Socket options on a loopback TCP connection and UDP socket: each change is read back, the
   keepalive idle time rounds up to whole seconds, and a multicast group on the loopback
   interface is joined, refused a second time and left. */
async std.string::string tune(u32 keepalive_ms, u32 hop_limit) throws std.error::fault {
    deadline (example.netlab.address::five_seconds()) {
        std.net::tcp_listener listener = await example.netlab.address::listen();
        std.net::socket_address endpoint = listener.local_address();
        std.net::tcp_stream client = await endpoint.connect();
        std.net::tcp_options wanted = client.get_options();
        wanted.nodelay = true;
        wanted.keepalive = o::some(std.time::duration_from_parts(
            (keepalive_ms / 1000u32) as i64, (keepalive_ms % 1000u32) * 1000000u32));
        wanted.hop_limit = hop_limit;
        std.net::tcp_set_options(&client, wanted);
        std.net::tcp_options seen = std.net::tcp_get_options(&client);
        std.string::string idle = keepalive_text(seen);
        std.string::string output =
            f"tcp nodelay={seen.nodelay} keepalive={idle} hop_limit={seen.hop_limit}\n";
        std.net::tcp_options quiet = seen;
        quiet.keepalive = o::none;
        client.set_options(quiet);
        std.string::string off = keepalive_text(client.get_options());
        std.string::string cleared = f"tcp keepalive={off}\n";
        output.append(cleared.as_str());
        std.net::socket_address local = example.netlab.address::loopback();
        std.net::udp_socket socket = await local.bind(false);
        std.net::udp_options datagram = std.net::udp_get_options(&socket);
        datagram.broadcast = true;
        datagram.multicast_hop_limit = 3u32;
        datagram.multicast_loop = false;
        std.net::udp_set_options(&socket, datagram);
        std.net::udp_options now = socket.get_options();
        std.string::string datagrams = f"udp broadcast={now.broadcast} multicast_hop_limit={now.multicast_hop_limit} multicast_loop={now.multicast_loop}\n";
        output.append(datagrams.as_str());
        /* Interface 1 is the loopback interface of the reference target. */
        std.net::ip_address group = std.net::parse_ip("239.1.2.3");
        socket.join_multicast(group, 1u32);
        bool repeated_refused = false;
        try {
            std.net::udp_join_multicast(&socket, group, 1u32);
        } catch (std.net::net_error failure) {
            repeated_refused = failure.code == std.net::error_code::address_in_use;
        }
        std.net::udp_leave_multicast(&socket, group, 1u32);
        bool unjoined_refused = false;
        try {
            socket.leave_multicast(std.net::parse_ip("239.1.2.4"), 1u32);
        } catch (std.net::net_error failure) {
            unjoined_refused = failure.code == std.net::error_code::address_not_available;
        }
        std.string::string membership = f"multicast joined=239.1.2.3 repeated_refused={repeated_refused} unjoined_refused={unjoined_refused}\n";
        output.append(membership.as_str());
        await (move socket).close();
        await (move client).close();
        await (move listener).close();
        return move output;
    }
}
