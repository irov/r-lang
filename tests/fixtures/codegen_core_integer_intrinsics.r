module test.codegen.core_integer_intrinsics;

i32 main() {
    try {
        o<i32> checked = core::checked_add_i32(40, 2);
        switch (checked) {
            case variant o::some(value): if (*value != 42) { throw TestAssertionFailed {.code = 1}; } break;
            case variant o::none: throw TestAssertionFailed {.code = 2};
        }
        o<i32> checked_2 = core::checked_add_i32(2147483647i32, 1);
        switch (checked_2) {
            case variant o::some(value): *value as void; throw TestAssertionFailed {.code = 3};
            case variant o::none: break;
        }
        if (core::wrapping_add_i32(2147483647i32, 1) != -2147483648i32) { throw TestAssertionFailed {.code = 4}; }
        if (core::wrapping_sub_i32(-2147483648i32, 1) != 2147483647i32) { throw TestAssertionFailed {.code = 5}; }
        if (core::wrapping_mul_u8(128u8, 2u8) != 0u8) { throw TestAssertionFailed {.code = 6}; }
        if (core::saturating_add_i32(2147483647i32, 1) != 2147483647i32) { throw TestAssertionFailed {.code = 7}; }
        if (core::saturating_sub_i32(-2147483648i32, 1) != -2147483648i32) { throw TestAssertionFailed {.code = 8}; }
        if (core::saturating_mul_u8(200u8, 2u8) != 255u8) { throw TestAssertionFailed {.code = 9}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
