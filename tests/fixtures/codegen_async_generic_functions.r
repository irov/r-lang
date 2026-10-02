module test.codegen.async_generic_functions;

@generic<T: send & unborrowed>
protected async T relay(T value) { return move value; }

@generic<T: send & unborrowed>
protected async T nested(T value) throws std.async::start_error {
    task<T> work = relay(move value);
    T result = await move work;
    return move result;
}

async i32 main() {
    try {
        try {
            i32 source = 7;
            task<i32> first = relay(move source);
            i32 copied = await move first;
            if (source != 7 || copied != 7) { throw TestAssertionFailed {.code = 1}; }
            own i32* owner = new i32(11);
            task<own i32* throws std.async::start_error> second = nested(move owner);
            own i32* result = await move second;
            if (*result != 11) { throw TestAssertionFailed {.code = 2}; }
            return 0;
        } catch (std.async::start_error error) { error as void; throw TestAssertionFailed {.code = 99}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
