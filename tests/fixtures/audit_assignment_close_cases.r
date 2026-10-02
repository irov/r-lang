module audit.assignment_close_cases;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { Resource value; };

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

error Failure {};

protected Resource fail_resource() throws Failure {
    throw Failure {};
}

i32 main() {
    try {
        Resource value = Resource {.marker = 1};
        Resource moved = move value;
        value = Resource {.marker = 2};
        drop value;
        drop moved;
        if (drop_trace(0) != 21) { throw TestAssertionFailed {.code = 2}; }

        TestStorage1 storage_retained = {.value = Resource {.marker = 3}};
        try {
            storage_retained.value = fail_resource();
            drop storage_retained;
            throw TestAssertionFailed {.code = 3};
        } catch (Failure error) {
            error as void;
            if (storage_retained.value.marker != 3) { drop storage_retained; throw TestAssertionFailed {.code = 4}; }
        }
        drop storage_retained;
        if (drop_trace(0) != 213) { throw TestAssertionFailed {.code = 5}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
