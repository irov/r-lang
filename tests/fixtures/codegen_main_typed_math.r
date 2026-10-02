module test.codegen.main_typed_math;

i32 main() {
    std.math::math_error failure = {.code = std.math::error_code::domain, .native_code = -42}; throw failure;
}
