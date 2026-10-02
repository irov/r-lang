module audit.async_switch_nested_break_targets;

protected async i32 break_runs_finally(bool leave) {
    i32 trace = 0;
    switch (0) {
        case 0:
            try {
                if (leave == true) {
                    break;
                } else {
                    trace = 2;
                }
            } finally {
                trace += 3;
            }
            trace += 10;
            break;
        default:
            return 100;
    }
    return trace;
}

async i32 main() {
    try {
        try {
            task<i32> first_operation = break_runs_finally(true);
            i32 first = await move first_operation;
            task<i32> second_operation = break_runs_finally(false);
            i32 second = await move second_operation;
            if (first != 3) {
                throw TestAssertionFailed {.code = 1};
            }
            if (second != 15) {
                throw TestAssertionFailed {.code = 2};
            }
            return 0;
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 3};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
