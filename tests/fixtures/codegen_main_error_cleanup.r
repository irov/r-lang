module test.codegen.main_error_cleanup;

bytes static_bytes = {};
thread_local bytes local_bytes = {};

i32 main() {
    unsafe { std.bytes::append_u8(&static_bytes, 1); }
    std.bytes::append_u8(&local_bytes, 2);
    std.io::io_error failure = {
        .code = std.io::error_code::permission_denied,
        .native_code = -9223372036854775807 - 1,
    };
    throw failure;
}
