module test.codegen.library_net_options;

// R-SLIB-NET-0012..0013 (M22): socket options read and changed field by field, validated
// before any change, keepalive rounded up to whole seconds, and multicast membership.

protected i32 tcp_checks(std.net::tcp_stream* stream)
    throws std.net::net_error, std.time::duration_error {
    std.net::tcp_options before = stream->get_options();
    std.net::tcp_options wanted = before;
    wanted.nodelay = true;
    wanted.keepalive = o::some(std.time::duration_from_seconds(30i64));
    wanted.hop_limit = 32u32;
    wanted.receive_buffer = 65536usize;
    stream->set_options(wanted);
    std.net::tcp_options after = std.net::tcp_get_options(stream);
    i32 status = 0;
    if (after.nodelay == false) { status = 1; }
    switch (after.keepalive) {
    case variant o::some(idle): if (std.time::duration_seconds(*idle) != 30i64) { status = 2; }
    case variant o::none: status = 3;
    }
    if (after.hop_limit != 32u32) { status = 4; }
    if (after.receive_buffer < 65536usize) { status = 5; }
    if (after.send_buffer != before.send_buffer) { status = 6; }
    if (status != 0) { return status; }
    /* Rounded up to whole seconds; then probes off. */
    std.net::tcp_options rounded = after;
    rounded.keepalive = o::some(std.time::duration_from_parts(1i64, 500000000u32));
    stream->set_options(rounded);
    std.net::tcp_options seen = stream->get_options();
    switch (seen.keepalive) {
    case variant o::some(idle): if (std.time::duration_seconds(*idle) != 2i64) { status = 7; }
    case variant o::none: status = 8;
    }
    std.net::tcp_options quiet = seen;
    quiet.keepalive = o::none;
    stream->set_options(quiet);
    std.net::tcp_options off = stream->get_options();
    switch (off.keepalive) {
    case variant o::some(idle): idle as void; status = 9;
    case variant o::none: break;
    }
    return status;
}

/* An invalid record changes nothing. */
protected i32 tcp_invalid(std.net::tcp_stream* stream) throws std.net::net_error {
    std.net::tcp_options current = stream->get_options();
    std.net::tcp_options invalid = current;
    invalid.nodelay = current.nodelay == false;
    invalid.hop_limit = 0u32;
    try {
        stream->set_options(invalid);
        return 11;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::unsupported) { return 12; }
    }
    std.net::tcp_options unchanged = stream->get_options();
    if (unchanged.nodelay != current.nodelay) { return 13; }
    return 0;
}

protected i32 udp_checks(std.net::udp_socket* socket) throws std.net::net_error, std.net::address_error {
    std.net::udp_options before = socket->get_options();
    std.net::udp_options wanted = before;
    wanted.broadcast = true;
    wanted.multicast_hop_limit = 4u32;
    wanted.multicast_loop = false;
    wanted.hop_limit = 17u32;
    socket->set_options(wanted);
    std.net::udp_options after = std.net::udp_get_options(socket);
    i32 status = 0;
    if (after.broadcast == false) { status = 21; }
    if (after.multicast_hop_limit != 4u32) { status = 22; }
    if (after.multicast_loop == true) { status = 23; }
    if (after.hop_limit != 17u32) { status = 24; }
    if (status != 0) { return status; }
    /* Interface 1 is the loopback interface of the reference target. */
    std.net::ip_address group = std.net::parse_ip("239.1.2.3");
    socket->join_multicast(group, 1u32);
    try {
        std.net::udp_join_multicast(socket, group, 1u32);
        return 25;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::address_in_use) { return 26; }
    }
    socket->leave_multicast(group, 1u32);
    try {
        std.net::udp_leave_multicast(socket, group, 1u32);
        return 27;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::address_not_available) { return 28; }
    }
    /* Zero lets the target route the group; a host without such a route has no address. */
    std.net::ip_address routed = std.net::parse_ip("239.1.2.4");
    try {
        socket->join_multicast(routed, 0u32);
        socket->leave_multicast(routed, 0u32);
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::address_not_available) { return 33; }
    }
    try {
        socket->join_multicast(std.net::parse_ip("10.0.0.1"), 0u32);
        return 29;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::invalid_address) { return 30; }
    }
    try {
        socket->join_multicast(std.net::parse_ip("ff02::1"), 0u32);
        return 31;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::unsupported) { return 32; }
    }
    return 0;
}

protected i32 udp6_checks(std.net::udp_socket* socket) throws std.net::net_error, std.net::address_error {
    std.net::ip_address group = std.net::parse_ip("ff05::1:5");
    socket->join_multicast(group, 1u32);
    try {
        socket->join_multicast(group, 1u32);
        return 41;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::address_in_use) { return 42; }
    }
    socket->leave_multicast(group, 1u32);
    try {
        socket->join_multicast(std.net::parse_ip("239.1.2.3"), 1u32);
        return 43;
    } catch (std.net::net_error failure) {
        if (failure.code != std.net::error_code::unsupported) { return 44; }
    }
    std.net::udp_options options = socket->get_options();
    options.multicast_hop_limit = 2u32;
    options.hop_limit = 200u32;
    socket->set_options(options);
    std.net::udp_options seen = socket->get_options();
    if (seen.multicast_hop_limit != 2u32 || seen.hop_limit != 200u32) { return 45; }
    return 0;
}

async i32 main() {
    try {
        std.net::socket_address local = {.address = std.net::parse_ip("127.0.0.1"), .port = 0u16,
                                         .scope_id = 0u32};
        std.net::listen_options listen = {.backlog = 4u32, .reuse_address = true, .v6_only = false};
        std.net::tcp_listener listener = await local.listen(listen);
        std.net::socket_address endpoint = listener.local_address();
        std.net::tcp_stream client = await endpoint.connect();
        std.net::tcp_connection accepted = await listener.accept();
        i32 status = tcp_checks(&client);
        if (status == 0) { status = tcp_invalid(&client); }
        std.net::udp_socket datagrams = await local.bind(false);
        if (status == 0) { status = udp_checks(&datagrams); }
        std.net::socket_address local6 = {.address = std.net::parse_ip("::1"), .port = 0u16,
                                          .scope_id = 0u32};
        std.net::udp_socket datagrams6 = await local6.bind(false);
        if (status == 0) { status = udp6_checks(&datagrams6); }
        await (move datagrams6).close();
        await (move client).close();
        drop accepted;
        await (move listener).close();
        await (move datagrams).close();
        return status;
    } catch (std.net::net_error failure) {
        failure as void;
        return 90;
    } catch (std.net::address_error failure) {
        failure as void;
        return 91;
    } catch (std.async::start_error failure) {
        failure as void;
        return 92;
    } catch (std.time::duration_error failure) {
        failure as void;
        return 93;
    }
}
