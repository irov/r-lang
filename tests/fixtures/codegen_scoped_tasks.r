module scope.probe;
async i32 value(i32 n) { return n; }
async i32 main() {
    try {
        try {
            task_scope(2) group {
                auto a = value(20);
                auto b = value(22);
                usize selected = await group.first(&a, &b);
                await group.all();
                i32 x = await move a;
                i32 y = await move b;
                if (selected > 1usize) { throw TestAssertionFailed {.code = 3}; }
                i32 chosen = x + y == 42 ? 0 : 3;
                return chosen;
            }
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 1}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
