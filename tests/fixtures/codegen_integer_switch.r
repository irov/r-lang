module test.codegen.integer_switch;

protected u32 next(u32* count) {
    *count += 1;
    return *count;
}

protected i32 classify(u32* count) {
    i32 result = 0;
    switch (next(count)) {
    case 0:
        result = 10;
        break;
    case 1:
        result = 20;
        break;
    case 2:
        result = 30;
        break;
    default:
        result = 40;
        break;
    }
    return result;
}

i32 main() {
    try {
        u32 count = 0;
        i32 result = classify(&count);
        if (count != 1) {
            throw TestAssertionFailed {.code = 1};
        }
        if (result != 20) {
            throw TestAssertionFailed {.code = 2};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
