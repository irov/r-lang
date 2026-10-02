module codegen.main;

protected i32 local_value(i32 value) {
    return value;
}

protected u32 wrapped_value(u32 value) {
    value += 2;
    return value;
}

protected i32 calculated_value(i32 left, i32 right) {
    i32 value = left + right;
    value += 3;
    value -= 1;
    value *= 2;
    value /= 2;
    value %= 17;
    return value;
}

protected u32 calculated_unsigned_value(u32 value) {
    value += 2;
    value -= 1;
    value *= 3;
    value /= 3;
    value %= 19;
    value *= -1;
    return value;
}

protected bool calculated_logic(bool left, bool right) {
    bool both = left && right;
    bool result = both || !left;
    return result;
}

i32 main() {
    try {
        i32 minimum = -2147483648;
        i32 value = local_value(9);
        i32 calculated = calculated_value(4, 5);
        u32 wrapped = wrapped_value(4294967295);
        u32 unsigned = calculated_unsigned_value(5);
        u32 wrapped_subtraction = 0;
        wrapped_subtraction -= 1;
        u32 wrapped_multiplication = 2147483648;
        wrapped_multiplication *= 2;
        bool accepted = true;
        bool logic_result = calculated_logic(false, false);
        bool short_and = false && ((1 / 0) == 0);
        bool short_or = true || ((1 / 0) == 0);
        while (value < 12) {
            value += 1;
        }
        i32 negative = -value;
        if (accepted == true) {
            if (logic_result == true) {
                if (short_and == false) {
                    if (short_or == true) {
                        if (value == 12) {
                            if (negative == -12) {
                                if (calculated == 11) {
                                    if (minimum < 0) {
                                        if (wrapped == 1) {
                                            if (unsigned == 4294967290) {
                                                if (wrapped_subtraction == 4294967295) {
                                                    if (wrapped_multiplication == 0) {
                                                        return 0;
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        throw TestAssertionFailed {.code = 1};
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
