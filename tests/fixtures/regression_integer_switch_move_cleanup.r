module test.regression.integer_switch_move_cleanup;

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

protected void consume_in_one_clause(i32 selected) {
    own Resource* retained = new Resource { .marker = 71 };
    switch (selected) {
    case 0:
        own Resource* consumed = move retained;
        drop consumed;
        return;
    default:
        break;
    }
}

i32 main() {
    try {
        i32 before = drop_count(0);
        consume_in_one_clause(1);
        i32 after = drop_count(0);
        if (after != before + 1) {
            throw TestAssertionFailed {.code = 1};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
