module semantic.io_close_input_success_reuse;

i32 invalid(std.io::input stream) throws std.async::start_error {
    task<void throws std.io::io_error> operation =
        std.io::close_input(move stream, o::none);
    drop stream;
    std.async::cancel(move operation);
    return 0;
}
