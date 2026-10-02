module example.service.client;
import example.service.wire;

/* Send one request and return the reply, or none when the service closed the connection
   without one. The request `hold` sends nothing and keeps its side open until the service
   closes the connection. */
async o<std.string::string> exchange(std.net::socket_address endpoint, std.string::string request)
    throws std.error::fault {
    std.net::tcp_stream stream = await endpoint.connect();
    array<u8> reply = std.alloc::bytes(64usize, 0u8);
    usize length = 0usize;
    bool hold = false;
    switch (request.as_str()) {
    case "hold": hold = true;
    default: break;
    }
    task_scope(1) io {
        if (hold == false) {
            await std.net::tcp_write_all_from(&stream, request.as_str());
            await stream.shutdown(std.net::shutdown_direction::write);
        }
        length += await example.service.wire::read_all(&stream, reply.as_slice_mut());
    }
    await (move stream).close();
    if (length == 0usize) { return o::none; }
    const u8[] text = reply.as_slice();
    return o::some(std.string::from_utf8(text[0usize..length]));
}
