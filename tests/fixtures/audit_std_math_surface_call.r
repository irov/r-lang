module audit.std_math_surface_call;

std.math::complex_f64 add_complex(
    std.math::complex_f64 left,
    std.math::complex_f64 right) {
    return std.math::add_complex_f64(left, right);
}

std.math::complex_f32 sqrt_complex(std.math::complex_f32 value)
    throws std.math::math_error {
    return std.math::sqrt_complex_f32(value);
}

std.error::error erase_math_error(std.math::math_error value) {
    return std.math::as_error(value);
}

i32 main() {
    try {
        f32 absolute = std.math::abs_f32(-2.0f32);
        c_double lower = 2.0f64 as c_double;
        c_double upper = 4.0f64 as c_double;
        c_double minimum = std.math::min_c_double(lower, upper);
        bool finite = std.math::is_finite_f64(1.0f64);
        std.math::binary_parts_f64 parts = std.math::split_binary_f64(8.0f64);
        parts as void;
        if ((absolute != 2.0f32) || (minimum != lower) || (finite == false)) {
            throw TestAssertionFailed {.code = 1};
        }
        try {
            f32 root = std.math::sqrt_f32(9.0f32);
            c_long_double logarithm =
                std.math::log_c_long_double(1.0f64 as c_long_double);
            if ((root != 3.0f32) || (logarithm != 0.0f64 as c_long_double)) {
                throw TestAssertionFailed {.code = 2};
            }
            return 0;
        } catch (std.math::math_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 3};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
