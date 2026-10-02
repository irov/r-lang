module test.codegen.main_typed_process;

i32 main() {
    std.process::process_error failure = {.code = std.process::error_code::permission_denied, .native_code = -42}; throw failure;
}
