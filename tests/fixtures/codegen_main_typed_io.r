module test.codegen.main_typed_io;

i32 main() {
    std.io::io_error failure = {.code = std.io::error_code::permission_denied, .native_code = -42}; throw failure;
}
