module test.codegen.async_drop;

protected async i32 child() {
    return 41;
}

async i32 main() {
    try {
        try {
            task<i32> operation = child();
            drop operation;
            return 0;
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 3};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
