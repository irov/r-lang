module test.semantic.stack_recursive_async;

/* R-FUNC-0004: an asynchronous function that starts itself is a call-graph cycle. */
async i32 descend(i32 value) throws std.async::start_error {
    if (value == 0) {
        return 0;
    }
    i32 next = value - 1;
    i32 rest = await descend(next);
    return rest + 1;
}

async i32 main() {
    try { return await descend(3) - 3; }
    catch (std.async::start_error failure) { return 1; }
}
