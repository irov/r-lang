module test.codegen.replacement;
error PreparationFailed {};

own i32* fail_before_commit(i32* attempts) throws PreparationFailed {
    *attempts += 1;
    throw PreparationFailed {};
}

i32 prepare(i32* count) { *count += 10; return 42; }

struct Tracked { own i32* data; };
drop(Tracked* self) { *(self->data) = 9; }

@generic<T: unborrowed>
T exchange(T* destination, T value) {
    T old = core::replace(destination, move value);
    return move old;
}

i32 main() {
    try {
        i32 number = 40;
        i32 old_number = exchange(&number, 42);
        i32 taken_number = core::take(&number);
        if (old_number != 40 || taken_number != 42 || number != 0) { throw TestAssertionFailed {.code = 1}; }
        i32 evaluations = 0;
        i32* place = &number;
        i32 replacement_number = prepare(&evaluations);
        i32 prepared = core::replace(place, replacement_number);
        if (evaluations != 10 || prepared != 0 || number != 42) { throw TestAssertionFailed {.code = 7}; }
        i32[2] items = {1, 2};
        i32[2] replacement_items = {3, 4};
        i32[2] previous_items = core::replace(&items, replacement_items);
        i32 element = core::take(&items[1]);
        if (previous_items[0] != 1 || previous_items[1] != 2 || element != 4 || items[1] != 0) { throw TestAssertionFailed {.code = 8}; }
        Tracked current = {.data = new i32(1)};
        Tracked next = {.data = new i32(2)};
        Tracked old = exchange(&current, move next);
        if (*(current.data) != 2 || *(old.data) != 1) { throw TestAssertionFailed {.code = 2}; }
        i32 attempts = 0;
        try {
            own i32* prepared_owner = fail_before_commit(&attempts);
            own i32* impossible = core::replace(&current.data, move prepared_owner);
            throw TestAssertionFailed {.code = 9};
        } catch (PreparationFailed failure) {
            if (attempts != 1 || *(current.data) != 2) { throw TestAssertionFailed {.code = 10}; }
        } finally {
            attempts += 10;
        }
        if (attempts != 11) { throw TestAssertionFailed {.code = 11}; }
        o<Tracked> slot = o::some(move old);
        o<Tracked> removed = core::take(&slot);
        switch (move removed) {
        case variant o::some(move value): if (*(value.data) != 1) { throw TestAssertionFailed {.code = 3}; } break;
        case variant o::none: throw TestAssertionFailed {.code = 4};
        }
        switch (move slot) {
        case variant o::some(move value): throw TestAssertionFailed {.code = 5};
        case variant o::none: break;
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
