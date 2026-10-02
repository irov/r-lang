module test.codegen.unreachable_catch;

/* A catch clause whose error the try body never throws: the generated C must not carry an
   unused label for it. */
i32 fill(array<u32>* values) throws std.array::push_error<u32> {
    values->push(7u32);
    return 0;
}

i32 main() {
    try {
        try {
            array<u32> values = std.array::create::<u32>();
            i32 status = fill(&values);
            if (len(values) != 1usize) { throw TestAssertionFailed {.code = 2}; }
            return status;
        } catch (std.array::push_error<u32> failure) { throw TestAssertionFailed {.code = 3}; }
          catch (std.alloc::alloc_error failure) { throw TestAssertionFailed {.code = 4}; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
