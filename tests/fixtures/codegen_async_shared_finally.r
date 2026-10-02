module test.codegen.async_shared_finally;

error move_error {
    own i32* payload;
    i32 code;
};

protected
async i32 shared_checked_cleanup() {
    i32 trace = 0;
    try {
        try {
            own i32* payload = new i32(41);
            throw move_error {
                .payload = move payload,
                .code = 7,
            };
        } finally {
            trace *= 10; trace += 1;
        }
    } catch (move_error error) {
        if (error.code != 7) {
            return 900;
        }
        trace *= 10; trace += 2;
    } finally {
        try {
            trace *= 10; trace += 3;
        } finally {
            trace *= 10; trace += 4;
        }
    }
    return trace;
}

protected
async void owned_error_through_nested_finally() throws move_error {
    try {
        own i32* payload = new i32(73);
        throw move_error {
            .payload = move payload,
            .code = 73,
        };
    } finally {
        try {
            i32 marker = 1;
            marker as void;
        } finally {
            i32 marker = 2;
            marker as void;
        }
    }
}

async i32 main() {
    try {
        try {
            task<i32> operation = shared_checked_cleanup();
            i32 trace = await move operation;
            if (trace != 1234) {
                throw TestAssertionFailed {.code = 1};
            }
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 2};
        }

        try {
            task<void throws move_error> operation = owned_error_through_nested_finally();
            await move operation;
            throw TestAssertionFailed {.code = 3};
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 4};
        } catch (move_error error) {
            if (error.code != 73) {
                throw TestAssertionFailed {.code = 5};
            }
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
