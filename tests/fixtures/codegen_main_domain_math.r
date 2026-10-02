module test.codegen.main_domain_math;

i32 main() {
    std.error::error failure = {.domain = std.error::domain::math, .code = 0, .native_code = -9223372036854775807 - 1};
    throw failure;
}
