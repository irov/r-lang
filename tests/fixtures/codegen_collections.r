module test.codegen.collections;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };

/* R-EXPR-0030: array and dict expressions, literal and comprehension forms, with the effects
   of the standard insertions handled by the enclosing try. */
i32 main() {
    try {
        TestStorage1 storage_total = {.value = 0};
        try {
            array<i32> small = [1, 2, 3];
            array<i32> empty = [];
            for (const i32* x in &small) { storage_total.value += *x; }
            if (storage_total.value != 6) { throw TestAssertionFailed {.code = 1}; }
            if (len(empty) != 0usize) { throw TestAssertionFailed {.code = 2}; }

            array<i32> squares = [x * x for (i32 x in 0..10) if (x % 2 == 0)];
            storage_total.value = 0;
            for (const i32* x in &squares) { storage_total.value += *x; }
            if (storage_total.value != 120) { throw TestAssertionFailed {.code = 3}; }

            array<i32> pairs = [x + y for (i32 x in 0..3) for (i32 y in 0..3) if (x < y)];
            if (len(pairs) != 3usize) { throw TestAssertionFailed {.code = 4}; }
            storage_total.value = 0;
            for (i32 p in &pairs) { storage_total.value += p; }
            if (storage_total.value != 6) { throw TestAssertionFailed {.code = 5}; }

            array<i32> doubled = [*x * 2 for (const i32* x in &small)];
            storage_total.value = 0;
            for (i32 d in &doubled) { storage_total.value += d; }
            if (storage_total.value != 12) { throw TestAssertionFailed {.code = 6}; }

            dict<i32, i32> doubles = {x: x * 2 for (i32 x in 0..4)};
            i32 three = 3;
            if (three not in doubles) { throw TestAssertionFailed {.code = 7}; }
            i32 nine = 9;
            if (nine in doubles) { throw TestAssertionFailed {.code = 8}; }

            dict<i32, i32> fixed = {1: 10, 2: 20, 2: 25};
            storage_total.value = 0;
            for (auto entry in &fixed) { storage_total.value += *(entry.value); }
            if (storage_total.value != 35) { throw TestAssertionFailed {.code = 9}; }
        } catch (std.array::push_error<i32> push_failure) {
            push_failure as void;
            throw TestAssertionFailed {.code = 20};
        } catch (std.dict::insert_error<i32, i32> insert_failure) {
            insert_failure as void;
            throw TestAssertionFailed {.code = 21};
        }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
