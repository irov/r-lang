module test.codegen.async_conditional_throw;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };
struct TestStorage2 { i32 value; };

error Error { own i32* payload; i32 code; };

protected async i32 step() { return 1; }

protected async i32 conditional(bool fail) throws std.async::start_error {
    task<i32> pending = step();
    i32 count = await move pending;
    TestStorage1 storage_result = {.value = 0};
    i32 divisor = 0;
    if (fail == true) { divisor = 1; }
    try {
        try {
            own i32* payload = new i32(41);
            throw ((count == 1) && (fail == true)) Error {
                .payload = move payload,
                .code = 1 / divisor,
            };
            drop payload;
            storage_result.value = 17;
        } finally {
            count += 10;
        }
    } catch (Error error) {
        storage_result.value = error.code;
    }
    task<i32> after = step();
    i32 resumed = await move after;
    return storage_result.value + 100 * count + 10000 * resumed;
}

protected async i32 explicit_if(bool fail) throws std.async::start_error {
    task<i32> pending = step();
    i32 count = await move pending;
    TestStorage2 storage_result = {.value = 0};
    i32 divisor = 0;
    if (fail == true) { divisor = 1; }
    try {
        try {
            own i32* payload = new i32(41);
            if ((count == 1) && (fail == true)) {
                throw Error {
                    .payload = move payload,
                    .code = 1 / divisor,
                };
            }
            drop payload;
            storage_result.value = 17;
        } finally {
            count += 10;
        }
    } catch (Error error) {
        storage_result.value = error.code;
    }
    task<i32> after = step();
    i32 resumed = await move after;
    return storage_result.value + 100 * count + 10000 * resumed;
}

async i32 main() {
    try {
        try {
            task<i32 throws std.async::start_error> first = conditional(false);
            i32 a = await move first;
            task<i32 throws std.async::start_error> second = explicit_if(false);
            i32 b = await move second;
            task<i32 throws std.async::start_error> third = conditional(true);
            i32 c = await move third;
            task<i32 throws std.async::start_error> fourth = explicit_if(true);
            i32 d = await move fourth;
            if (a != b || a != 11117 || c != d || c != 11101) { throw TestAssertionFailed {.code = 1}; }
        } catch (std.async::start_error error) {
            throw TestAssertionFailed {.code = 2};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
