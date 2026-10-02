module example.process_status.main;

// A small child process for checking how a supervisor handles termination.
i32 main(const str[] arguments) {
    usize count = len(arguments);
    if (count == 1usize) { return 0; }
    bool abrupt = std.bytes::equal(arguments[1], "abort");
    if (abrupt == true) { std.process::abort(); }
    if (count != 2usize) { return 64; }
    try {
        i32 status = std.convert::parse_i32(arguments[1], 10u32);
        std.process::exit(status);
    } catch (std.convert::parse_error failure) { return 64; }
}
