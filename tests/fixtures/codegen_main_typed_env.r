module test.codegen.main_typed_env;

i32 main() {
    std.env::env_error failure = {.code = std.env::error_code::permission_denied, .native_code = -42}; throw failure;
}
