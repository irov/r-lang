module test.regression.short_circuit_move_cleanup;

struct Resource {
    i32 marker;
};

protected i32 drop_count(i32 increment) {
    static i32 count = 0;
    unsafe {
        count += increment;
        return count;
    }
}

drop(Resource* self) {
    self->marker as void;
    i32 ignored = drop_count(1);
    ignored as void;
}

protected bool consume(own Resource* value) {
    drop value;
    return true;
}

protected void skipped_and() {
    own Resource* retained = new Resource { .marker = 47 };
    bool result = false && consume(move retained) && consume(move retained);
    result as void;
}

protected void skipped_or() {
    own Resource* retained = new Resource { .marker = 53 };
    bool result = true || consume(move retained) || consume(move retained);
    result as void;
}

i32 main() {
    try {
        i32 before = drop_count(0);
        skipped_and();
        i32 after_and = drop_count(0);
        if (after_and != before + 1) {
            throw TestAssertionFailed {.code = 1};
        }
        skipped_or();
        i32 after_or = drop_count(0);
        if (after_or != after_and + 1) {
            throw TestAssertionFailed {.code = 2};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
