module test.codegen.async_u64;

protected async u64 echo(u64 value) {
    return value;
}

async i32 main() {
    try {
        u64 input = 4294967296;
        try {
            task<u64> operation = echo(input);
            if (input != 4294967296) {
                throw TestAssertionFailed {.code = 4};
            }
            u64 value = await move operation;
            if (value == input) {
                return 0;
            }
            throw TestAssertionFailed {.code = 5};
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 3};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
