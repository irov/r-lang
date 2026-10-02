module audit.async_assignment_array_reallocation_close;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };

protected i32 read(const i32* source) {
    return *source;
}

protected i32 write(i32* target) {
    *target = 7;
    return 9;
}

struct Pair {
    i32 first;
    i32 second;
};

async i32 main() {
    try {
        try {
            TestStorage1 storage_scalar = {.value = 4};
            storage_scalar.value = read(&storage_scalar.value) + 1;
            if (storage_scalar.value != 5) { throw TestAssertionFailed {.code = 1}; }

            Pair pair = Pair {.first = 1, .second = 2};
            pair.first = write(&pair.second);
            if (pair.first != 9 || pair.second != 7) { throw TestAssertionFailed {.code = 2}; }

            array<i32> values = std.array::with_capacity::<i32>(1);
            std.array::push(&values, 1);
            i32[] destination = std.array::as_slice_mut(&values);
            destination[0] = 3;
            std.array::push(&values, 2);
            const i32[] result = std.array::as_slice(&values);
            i32 status = result[0] == 3 && result[1] == 2 ? 0 : 5;
            drop values;
            return status;
        } catch (std.array::push_error<i32> error) {
            error as void;
            throw TestAssertionFailed {.code = 3};
        } catch (std.alloc::alloc_error error) {
            error as void;
            throw TestAssertionFailed {.code = 4};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
