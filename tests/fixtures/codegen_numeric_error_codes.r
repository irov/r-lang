module test.codegen.numeric_error_codes;

i32 main() {
    try {
        try {
            std.time::duration bad = std.time::duration_from_parts(0i64, 1000000000u32);
            throw TestAssertionFailed {.code = 1};
        } catch (std.time::duration_error failure) {
            if (failure != std.time::duration_error::invalid_nanoseconds) { throw TestAssertionFailed {.code = 2}; }
        }
        try {
            std.time::duration large = std.time::duration_from_seconds(9223372036854775807i64);
            std.time::duration one = std.time::duration_from_seconds(1i64);
            std.time::duration bad = std.time::duration_add(large, one);
            throw TestAssertionFailed {.code = 3};
        } catch (std.time::duration_error failure) {
            if (failure != std.time::duration_error::overflow) { throw TestAssertionFailed {.code = 4}; }
        }
        std.math::error_code domain = std.math::error_code::domain;
        std.math::error_code pole = std.math::error_code::pole;
        std.math::error_code overflow = std.math::error_code::overflow;
        std.math::error_code underflow = std.math::error_code::underflow;
        if (domain == pole || pole == overflow || overflow == underflow) { throw TestAssertionFailed {.code = 5}; }
        std.time::error_code invalid = std.time::error_code::invalid_value;
        std.time::error_code unavailable = std.time::error_code::unavailable;
        std.time::error_code cancelled = std.time::error_code::cancelled;
        std.time::error_code other = std.time::error_code::other;
        if (invalid == unavailable || unavailable == cancelled || cancelled == other) { throw TestAssertionFailed {.code = 6}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
