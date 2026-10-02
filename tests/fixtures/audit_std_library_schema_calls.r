module audit.std_library_schema_calls;

i32 main() {
    try {
        array<i32> values = std.array::create::<i32>();
        usize capacity = std.array::capacity(&values);
        std.array::clear(&values);

        list<i32> nodes = std.list::create::<i32>();
        std.list::clear(&nodes);

        dict<i32, i32> entries = std.dict::create::<i32, i32>();
        std.dict::clear(&entries);

        try {
            own i32* owner = std.alloc::try_new(17);
            i32 value = std.alloc::into_value(move owner);
            return value - 17 + capacity as i32;
        } catch (std.alloc::new_error<i32> failure) {
            throw TestAssertionFailed {.code = 1};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
