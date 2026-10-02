module regression.async_main_constant_switch_startup_args;

protected async i32 child() {
    return 7;
}

async i32 main(const str[] args) {
    try {
        try {
            switch (0) {
            case 0:
                return 0;
            case 1:
                i32 waited = await child();
                usize count = len(args);
                if (count == 0) {
                    return waited;
                }
                throw TestAssertionFailed {.code = 1};
            default:
                throw TestAssertionFailed {.code = 2};
            }
        } catch (std.async::start_error failure) {
            failure as void;
            throw TestAssertionFailed {.code = 3};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
