module example.netlab.address;

std.net::socket_address loopback() throws std.net::address_error {
    std.net::ip_address ip = std.net::parse_ip("127.0.0.1");
    return std.net::socket_address { .address = ip, .port = 0u16, .scope_id = 0u32 };
}

// A listener on an ephemeral loopback port.
async std.net::tcp_listener listen()
    throws std.net::address_error, std.net::net_error, std.async::start_error {
    std.net::listen_options options = {.backlog = 4u32, .reuse_address = true, .v6_only = false};
    std.net::socket_address local = loopback();
    return await local.listen(options);
}

// The deadline that every network exchange of a command shares.
std.time::instant five_seconds() throws std.time::time_error {
    std.time::instant now = std.time::monotonic_now();
    return now.add(std.time::duration_from_seconds(5i64));
}

std.string::string describe(std.net::socket_address value) throws std.alloc::alloc_error {
    std.string::string text = std.net::format_ip(value.address);
    return f"address={text} port={value.port} scope={value.scope_id}";
}

async std.string::string resolve(std.string::string host, u16 port, std.net::family family)
    throws std.net::net_error, std.async::start_error, std.alloc::alloc_error, std.time::time_error {
    str text = host.as_str();
    deadline (five_seconds()) {
        array<std.net::socket_address> addresses = await std.net::resolve(text, port, family);
        std.string::string output = std.string::create();
        for (const std.net::socket_address* entry in &addresses) {
            std.string::string line = describe(*entry);
            str content = line.as_str();
            output.append(content);
            output.append("\n");
        }
        return move output;
    }
}
