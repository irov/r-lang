module audit.move_field_assignment_replacement_cleanup;

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

struct Holder { Resource resource; };

i32 main() {
    try {
        Holder holder = Holder {.resource = Resource {.marker = 1}};
        holder.resource = Resource {.marker = 2};
        drop holder;
        if (drop_trace(0) != 12) { throw TestAssertionFailed {.code = 2}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
