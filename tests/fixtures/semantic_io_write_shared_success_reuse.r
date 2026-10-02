module semantic.io_write_shared_success_reuse;

void probe(const std.io::output* stream, arc (array<u8>) buffer)
    throws std.async::start_error {
    task<std.io::shared_write_result> operation =
        std.io::write_shared(stream, move buffer, 0, 1, o::none);
    drop buffer;
    std.async::cancel(move operation);
}
