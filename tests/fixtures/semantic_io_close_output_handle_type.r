module semantic.io_close_output_handle_type;

i32 invalid(std.io::input stream) throws std.async::start_error {
    task<void throws std.io::io_error> started =
        std.io::close_output(move stream, o::none);
    std.async::cancel(move started);
    return 0;
}
