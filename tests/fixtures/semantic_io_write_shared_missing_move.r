module semantic.io_write_shared_missing_move;

void probe(const std.io::output* stream, arc (array<u8>) buffer)
    throws std.async::start_error {
    task<std.io::shared_write_result> started =
        std.io::write_shared(stream, buffer, 0, 1, o::none);
    std.async::cancel(move started);
}
