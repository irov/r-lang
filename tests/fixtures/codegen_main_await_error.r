module test.codegen.main_await_error;
async void child() throws std.io::io_error {
    std.io::io_error failure = {.code = std.io::error_code::broken_pipe, .native_code = 32};
    throw failure;
}
async i32 main(const str[] args) {
    args as void;
    await child();
    return 0;
}
