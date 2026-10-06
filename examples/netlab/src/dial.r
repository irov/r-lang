module example.netlab.dial;
import std.net;
import example.netlab.address;

/* The two candidates in order; a failed allocation reports resource_exhausted. */
array<std.net::socket_address> candidates_of(std.net::socket_address first,
                                             std.net::socket_address second)
    throws std.net::net_error {
    array<std.net::socket_address> candidates = std.array::create();
    try {
        candidates.push(first);
        candidates.push(second);
    } catch (std.array::push_error<std.net::socket_address> failure) {
        failure as void;
        throw std.net::net_error {.code = std.net::error_code::resource_exhausted,
                                  .native_code = 0i64};
    }
    return move candidates;
}

/* A loopback listener reached by name, the resolver's addresses tried in order
   (std.net::tcp_connect_name), then through an explicit candidate list whose first address has
   no listener (std.net::tcp_connect_any). */
@scoped
async std.string::string dial(str host) throws std.error::fault {
    deadline (example.netlab.address::five_seconds()) {
        std.net::tcp_listener listener = await example.netlab.address::listen();
        std.net::socket_address endpoint = listener.local_address();
        std.net::tcp_listener closed = await example.netlab.address::listen();
        std.net::socket_address refusing = closed.local_address();
        await (move closed).close();
        u16 port = endpoint.port;
        std.string::string output = std.string::create();
        task_scope(1) named {
            std.net::tcp_stream first = await std.net::tcp_connect_name(host, port);
            std.net::tcp_connection accepted = await listener.accept();
            bool matched = (first.local_address()).port == accepted.peer.port;
            std.string::string line = f"named connected matched={matched}\n";
            output.append(line);
            drop accepted;
            await (move first).close();
        }
        array<std.net::socket_address> candidates = candidates_of(refusing, endpoint);
        std.net::tcp_stream second = await std.net::tcp_connect_any(move candidates);
        std.net::tcp_connection accepted = await listener.accept();
        bool matched = (second.local_address()).port == accepted.peer.port;
        std.string::string line = f"listed connected matched={matched} after_refusal=true\n";
        output.append(line);
        drop accepted;
        await (move second).close();
        await (move listener).close();
        return move output;
    }
}
