module test.codegen.async_function_items;

async i32 increment(i32 value) { return value + 1; }
@generic<F: async fn once(i32) -> i32>
async i32 invoke(F operation, i32 value) throws std.async::start_error {
    return await (move operation).call(value);
}

async i32 main() {
    try {
        try {
            auto operation = increment;
            i32 direct = await operation(41);
            i32 generic = await invoke(increment, 41);
            generic as void;
            i32 selected = direct == 42 && generic == 42 ? 0 : 1;
            return selected;
        } catch (std.async::start_error failure) { failure as void; throw TestAssertionFailed {.code = 2}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
