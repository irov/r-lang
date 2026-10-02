module test.codegen.async_math_records;

async i32 main() {
    try {
        std.math::complex_f64 input = { .real = 3.0, .imag = 4.0 };
        input.real = 5.0;
        std.math::complex_f64 copy = input;
        f64 magnitude = std.math::magnitude_complex_f64(copy);
        if (magnitude < 6.4 || magnitude > 6.41) { return 1; }
        std.math::binary_parts_f64 binary = std.math::split_binary_f64(12.0);
        if (binary.fraction != 0.75 || binary.exponent != 4) { return 2; }
        std.math::fraction_parts_c_long_double parts = std.math::split_fraction_c_long_double(3.25 as c_long_double);
        if (parts.whole != (3.0 as c_long_double) || parts.fraction != (0.25 as c_long_double)) { return 3; }
        return 0;
    } catch (std.math::math_error failure) { return 4; }
}
