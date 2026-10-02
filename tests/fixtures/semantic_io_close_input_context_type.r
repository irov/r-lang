module semantic.io_close_input_context_type;

i32 invalid(std.io::input stream) throws std.async::start_error {
    task<void> started =
        std.io::close_input(move stream, o::none);
    std.async::cancel(move started);
    return 0;
}
