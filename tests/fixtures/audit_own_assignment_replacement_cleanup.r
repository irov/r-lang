module audit.own_assignment_replacement_cleanup;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { own Resource* value; };

protected i32 drop_trace(i32 marker) {
    static i32 trace = 0;
    unsafe {
        if (marker != 0) { trace = trace * 10 + marker; }
        return trace;
    }
}

struct Resource { i32 marker; };

drop(Resource* self) {
    i32 ignored = drop_trace(self->marker);
    ignored as void;
}

i32 main() {
    try {
        TestStorage1 storage_value = {.value = new Resource {.marker = 1}};
        storage_value.value = new Resource {.marker = 2};
        drop storage_value;
        if (drop_trace(0) != 12) { throw TestAssertionFailed {.code = 2}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
