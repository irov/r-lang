module test.codegen.stack_recursive_rc;

/* R-FUNC-0004: an rc chain is destroyed iteratively once the last strong handle goes. */
thread_local i64 dropped = 0;
thread_local i64 last_value = -1;
thread_local bool ordered = true;

struct Node {
    i64 value;
    o<rc Node> next;
};

drop(Node* self) {
    if (dropped != 0 && self->value + 1 != last_value) { ordered = false; }
    last_value = self->value;
    dropped += 1;
}

i32 main() {
    try {
        o<rc Node> head = o::none;
        i64 index = 0;
        while (index < 1000000) {
            rc Node fresh = new rc Node { .value = index, .next = move head };
            head = o::some(move fresh);
            index += 1;
        }
        rc Node shared = new rc Node { .value = -5, .next = o::none };
        rc Node other = std.rc::clone(&shared);
        drop head;
        if (dropped != 1000000) { throw TestAssertionFailed {.code = 1}; }
        if (last_value != 0) { throw TestAssertionFailed {.code = 2}; }
        if (ordered == false) { throw TestAssertionFailed {.code = 3}; }
        drop shared;
        if (dropped != 1000000) { throw TestAssertionFailed {.code = 4}; }
        drop other;
        if (dropped != 1000001) { throw TestAssertionFailed {.code = 5}; }
        if (last_value != -5) { throw TestAssertionFailed {.code = 6}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
