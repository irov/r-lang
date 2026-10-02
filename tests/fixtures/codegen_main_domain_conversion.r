module test.codegen.main_domain_conversion;

i32 main() {
    std.error::error failure = {.domain = std.error::domain::conversion, .code = 0, .native_code = -9223372036854775807 - 1};
    throw failure;
}
