module test.codegen.main_typed_time;

i32 main() {
    std.time::time_error failure = {.code = std.time::error_code::unavailable, .native_code = -42}; throw failure;
}
