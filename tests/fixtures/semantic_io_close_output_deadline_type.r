module semantic.io_close_output_deadline_type;

i32 invalid(std.io::output stream) throws std.async::start_error {
    task<void throws std.io::io_error> started =
        std.io::close_output(move stream, 1);
    std.async::cancel(move started);
    return 0;
}
