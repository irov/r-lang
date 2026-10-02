module test.codegen.async_atomic_initialization;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


struct Counter {
    au32 value;
};

protected async au32 scalar() {
    au32 value = 41;
    return move value;
}

protected async Counter aggregate() {
    Counter value = Counter { .value = 42 };
    return move value;
}

async i32 main() {
    try {
        try {
            task<au32> scalar_operation = scalar();
            au32 scalar_value = await move scalar_operation;
            test_observe(&scalar_value);
            task<Counter> aggregate_operation = aggregate();
            Counter aggregate_value = await move aggregate_operation;
            test_observe(&aggregate_value);
            return 0;
        } catch (std.async::start_error error) {
            error as void;
            throw TestAssertionFailed {.code = 1};
        }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
