module test.codegen.async_sync_call;

protected i32 add(const i32* value, i32 increment) {
    return *value + increment;
}

protected own i32* forward(own i32* value) {
    return move value;
}

protected void consume(own i32* value) {
    drop value;
    return;
}

protected async i32 one() {
    return 1;
}

async i32 main() {
    try {
        i32 base = 40;
        i32 before = add(&base, 1);
        own i32* original = new i32(before);
        own i32* forwarded = forward(move original);
        try {
            task<i32> operation = one();
            i32 awaited = await move operation;
            i32 after = add(&base, awaited);
            if (before != 41) {
                throw TestAssertionFailed {.code = 3};
            }
            if (after != 41) {
                throw TestAssertionFailed {.code = 4};
            }
            if (*forwarded != 41) {
                throw TestAssertionFailed {.code = 5};
            }
            consume(move forwarded);
            return 0;
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 2};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
