module test.codegen.async_parameter_reassignment;

async i32 replace(i32 count, bytes data) throws std.async::start_error, std.time::time_error {
    count += 1;
    bytes replacement = {};
    bytes initialized = move replacement;
    drop data;
    await std.time::sleep_for(std.time::duration_from_seconds(0i64));
    bytes consumed = move initialized;
    bytes fresh = {};
    initialized = move fresh;
    count += 2;
    if (len(initialized) != 0usize || len(consumed) != 0usize) { return -1; }
    return count;
}

async i32 main() {
    try {
        try {
            bytes input = std.alloc::bytes(3usize, 1u8);
            i32 result = await replace(4, move input);
            if (result != 7) { throw TestAssertionFailed {.code = 1}; }
            return 0;
        } catch (std.async::start_error failure) { throw TestAssertionFailed {.code = 2}; }
        catch (std.time::time_error failure) { throw TestAssertionFailed {.code = 3}; }
        catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 4}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
