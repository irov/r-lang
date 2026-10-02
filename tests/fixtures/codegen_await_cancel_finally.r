module test.codegen.async_cancel_finally;

protected
async i32 child() {
    return 7;
}

protected
async i32 suspended_cleanup() throws std.async::start_error {
    i32 trace = 0;
    try {
        try {
            i32 value = await child();
            trace += value;
        } finally {
            trace *= 10; trace += 1;
        }
    } finally {
        trace *= 10; trace += 2;
    }
    return trace;
}

async i32 main() {
    try {
        try {
            i32 trace = await suspended_cleanup();
            if (trace != 712) {
                throw TestAssertionFailed {.code = 1};
            }
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 2};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
