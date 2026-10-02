module audit.async_aggregate_checked_initializer_cleanup;

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

struct Inner {
    Resource second;
    i32 checked;
};

struct Outer {
    Resource first;
    Inner inner;
};

error Failure {};

protected i32 fail() throws Failure {
    throw Failure {};
}

async i32 main() {
    try {
        i32 before = drop_trace(0);
        try {
            Outer value = {
                .first = Resource {.marker = 1},
                .inner = Inner {
                    .second = Resource {.marker = 2},
                    .checked = fail(),
                },
            };
            drop value;
            throw TestAssertionFailed {.code = 1};
        } catch (Failure error) {
            error as void;
        }
        i32 after = drop_trace(0);
        if (after != before * 100 + 21) { throw TestAssertionFailed {.code = 2}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
