module test.codegen.main_domain_time;

async i32 main() {
    std.error::error failure = {.domain = std.error::domain::time, .code = 0, .native_code = -9223372036854775807 - 1};
    throw failure;
}
