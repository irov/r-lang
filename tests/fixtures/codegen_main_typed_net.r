module test.codegen.main_typed_net;

i32 main() {
    std.net::net_error failure = {.code = std.net::error_code::permission_denied, .native_code = -42}; throw failure;
}
