module test.codegen.main_typed_fs;

i32 main() {
    std.fs::fs_error failure = {.code = std.fs::error_code::permission_denied, .native_code = -42}; throw failure;
}
