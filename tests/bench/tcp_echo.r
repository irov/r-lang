module bench.tcp_echo;

/* 20 000 exchanges of 4 KiB over a loopback TCP connection: write_all_from on the client,
   read_into on the accepted stream, both through the scoped operations. */
async i32 main() {
    std.net::socket_address local = std.net::socket_address {
        .address = std.net::parse_ip("127.0.0.1"), .port = 0u16, .scope_id = 0u32 };
    std.net::listen_options options = { .backlog = 4u32, .reuse_address = true, .v6_only = false };
    std.net::tcp_listener listener = await local.listen(options, o::none);
    std.net::socket_address endpoint = listener.local_address();
    std.net::tcp_stream client = await endpoint.connect(o::none);
    std.net::tcp_connection connection = await listener.accept(o::none);
    await (move listener).close(o::none);
    bytes payload = std.alloc::bytes(4096usize, 7u8);
    bytes window = std.alloc::bytes(4096usize, 0u8);
    usize iterations = 20_000usize;
    usize total = 0usize;
    bool broken = false;
    task_scope(1) exchange {
        for (usize index = 0usize; index < iterations && broken == false; index += 1usize) {
            const u8[] out = payload.as_slice();
            await std.net::tcp_write_all_from(&client, out, o::none);
            usize received = 0usize;
            while (received < 4096usize) {
                u8[] slice = window.as_slice_mut();
                usize count = await std.net::tcp_read_into(&connection.stream, slice[received..4096usize], o::none);
                if (count == 0usize) { broken = true; break; }
                received += count;
            }
            total += received;
        }
    }
    drop payload; drop window; drop connection;
    await (move client).close(o::none);
    if (broken == true) { return 70; }
    return (total % 109usize) as i32;
}
