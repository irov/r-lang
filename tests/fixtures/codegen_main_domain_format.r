module test.codegen.main_domain_format;

async i32 main() {
    std.error::error failure = {.domain = std.error::domain::format, .code = 0, .native_code = -9223372036854775807 - 1};
    throw failure;
}
