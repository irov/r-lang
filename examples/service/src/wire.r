module example.service.wire;

/* The bytes that the peer sends before it ends its side, at most the buffer: each read is one
   line and runs under the deadline of the calling task. */
@scoped
async usize read_all(const std.net::tcp_stream* stream, u8[] buffer) throws std.error::fault {
    usize length = 0usize;
    usize count = 1usize;
    task_scope(1) io {
        while (count != 0usize && length < len(buffer)) {
            count = await std.net::tcp_read_into(stream, buffer[length..len(buffer)]);
            length += count;
        }
    }
    return length;
}
