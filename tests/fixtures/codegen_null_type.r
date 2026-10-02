module test.codegen.null_type;

error Missing { Value, };

i32 pick(null_t absent) { return 1; }
i32 pick(const i32*? pointer) { i32 chosen = pointer == null ? 2 : 3; return chosen; }
i32 pick(i32 value) { return 4; }
i32 pick(bool value) { return 5; }
i32 pick(own i32*? pointer) { i32 chosen = pointer == null ? 6 : 7; return chosen; }
i32 pick(raw fn?() -> void callback) { return 8; }

@generic<T: copy>
i32 generic_pick(T value) { return 10; }
i32 generic_pick(null_t absent) { return 9; }

@generic<T: copy>
i32 pair(null_t absent, T value) { return 11; }

@generic<T: copy>
i32 fallback(const T*? pointer, T value) { i32 chosen = pointer == null ? 12 : 13; return chosen; }
i32 fallback(i32 first, i32 second) { return 14; }

struct Receipt { i32 total; i32 missing; };
void Receipt::append(Receipt* this, null_t absent) { this->missing += 1; }
void Receipt::append(Receipt* this, i32 value) { this->total += value; }

i32 required(null_t absent) throws Missing { throw Missing::Value; }
i32 required(i32 value) { return value; }

i32 main() {
    try {
        i32 literal = pick(null);
        i32 grouped = pick((/* sentinel */null));
        const i32*? pointer = null;
        i32 typed_pointer = pick(pointer);
        i32 number = pick(0);
        i32 boolean = pick(false);
        own i32*? owner = null;
        i32 typed_owner = pick(move owner);
        raw fn?() -> void callback = null;
        i32 typed_callback = pick(callback);
        if (literal != 1 || grouped != 1 || typed_pointer != 2) { throw TestAssertionFailed {.code = 1}; }
        if (number != 4 || boolean != 5 || typed_owner != 6 || typed_callback != 8) { throw TestAssertionFailed {.code = 2}; }
        i32 selected = generic_pick(null);
        i32 selected_value = generic_pick(42);
        i32 paired = pair(null, 42);
        if (selected != 9 || selected_value != 10 || paired != 11) { throw TestAssertionFailed {.code = 3}; }
        i32 inferred = fallback(null, 42);
        if (inferred != 12) { throw TestAssertionFailed {.code = 7}; }
        Receipt receipt = { .total = 0, .missing = 0 };
        receipt.append(null);
        receipt.append(42);
        Receipt::append(&receipt, null);
        if (receipt.total != 42 || receipt.missing != 2) { throw TestAssertionFailed {.code = 4}; }
        i32 present = required(42);
        if (present != 42) { throw TestAssertionFailed {.code = 5}; }
        try {
            i32 missing = required(null);
            throw TestAssertionFailed {.code = 6};
        } catch (Missing failure) { return 0; }
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
