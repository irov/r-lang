module test.regression.len_after_async_move;

usize moved_async_length(const std.io::input* stream, bytes buffer)
    throws std.async::start_error {
    task<std.io::read_result> operation = std.io::read(stream, move buffer, o::none);
    std.async::cancel(move operation);
    usize count = len(buffer);
    return count;
}
