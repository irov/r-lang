module test.codegen.async_cancel_finally;

// The C wrapper lets this task finish only after suspended_cleanup has suspended on child, so
// that child suspends on it first even when it starts on the thread of its caller.
protected
async void hold() {
}

protected
async i32 child() {
    try {
        task<void> held = hold();
        await move held;
    } catch (std.async::start_error error) {
        error as void;
        return 0;
    }
    return 7;
}

// The trace is owned storage so that the C wrapper reads it when the cancelled task releases it.
protected
async i32 suspended_cleanup() throws std.async::start_error {
    own i32* trace = new i32(0);
    try {
        try {
            task<i32> operation = child();
            i32 value = await move operation;
            *trace += value;
        } finally {
            *trace *= 10; *trace += 1;
        }
    } finally {
        *trace *= 10; *trace += 2;
    }
    return *trace;
}

async i32 main() {
    try {
        task_scope(1) outer {
            task<i32 throws std.async::start_error> operation = suspended_cleanup();
            // The C wrapper holds this cancel until suspended_cleanup resumes after child has
            // completed; outer.all() then waits for the cancelled task to finish its cleanup.
            std.async::cancel(move operation);
            await outer.all();
        }
    } catch (std.async::start_error error) {
        error as void;
        return 2;
    }
    return 0;
}
