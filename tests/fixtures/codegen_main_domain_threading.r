module test.codegen.main_domain_threading;

async i32 main(const str[] args) {
    std.error::error failure = {.domain = std.error::domain::threading, .code = 0, .native_code = -9223372036854775807 - 1};
    throw failure;
}
