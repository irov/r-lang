module test.codegen.async_module_storage;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };

thread_local i32 thread_counter = 2;
i32 shared_counter = 3;

protected async i32 step() {
    TestStorage1 storage_shared_value = {.value = 0};
    thread_counter += 2;
    unsafe {
        shared_counter += 3;
        storage_shared_value.value = shared_counter;
    }
    return thread_counter + storage_shared_value.value;
}

async i32 main() {
    try {
        try {
            i32 first = await step();
            i32 second = await step();
            if (first != 10) {
                throw TestAssertionFailed {.code = 2};
            }
            if (second != 13 && second != 15) {
                throw TestAssertionFailed {.code = 3};
            }
            return 0;
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 1};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
