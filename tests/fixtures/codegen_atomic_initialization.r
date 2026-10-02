module test.codegen.atomic_initialization;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


struct Counter {
    au32 value;
};

au32 pass_scalar(au32 value) {
    return move value;
}

Counter pass_counter(Counter value) {
    return move value;
}

i32 main() {
    au32 scalar = 41;
    Counter aggregate = Counter { .value = 42 };
    au32 scalar_result = pass_scalar(move scalar);
    test_observe(&scalar_result);
    Counter aggregate_result = pass_counter(move aggregate);
    test_observe(&aggregate_result);
    return 0;
}
