module test.codegen.range_for;

// Shared mutable storage preserves the update and cleanup scenario under test.
struct TestStorage1 { i32 value; };

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


/* R-STMT-0014: range-for over ranges, borrowed sequences, list and dict cursors, and
   core::Iterator implementations (R-TYPE-0046). */
struct Counter {
    i32 next_value;
    i32 limit;
};

impl core::Iterator for Counter {
    type Item = i32;
    o<i32> next(Counter* this) {
        if (this->next_value >= this->limit) { return o::none; }
        i32 value = this->next_value;
        this->next_value += 1;
        return o::some(value);
    }
};

@generic<I: core::Iterator>
i32 count_items(I* source) {
    i32 count = 0;
    for (auto item in source) { count += 1; }
    return count;
}

i32 main() {
    try {
        i32 total = 0;
        for (i32 i in 0..10) { total += i; }
        if (total != 45) { throw TestAssertionFailed {.code = 1}; }

        i32 total_2 = 0;
        for (auto i in 5..8) { total_2 += i; }
        if (total_2 != 18) { throw TestAssertionFailed {.code = 2}; }

        i32 total_3 = 0;
        for (u8 i in 0..4) {
            if (i == 2) { continue; }
            if (i == 3) { break; }
            total_3 += 1;
        }
        if (total_3 != 2) { throw TestAssertionFailed {.code = 3}; }

        i32[4] fixed = {1, 2, 3, 4};
        i32 total_4 = 0;
        for (const i32* x in &fixed) { total_4 += *x; }
        if (total_4 != 10) { throw TestAssertionFailed {.code = 4}; }
        for (i32* x in &fixed) { *x = *x * 2; }
        i32 total_5 = 0;
        for (i32 x in &fixed) { total_5 += x; }
        if (total_5 != 20) { throw TestAssertionFailed {.code = 5}; }

        const i32[] window = &fixed;
        TestStorage1 storage_total_6 = {.value = 0};
        for (auto x in &window) { storage_total_6.value += *x; }
        if (storage_total_6.value != 20) { throw TestAssertionFailed {.code = 6}; }

        try {
            array<i32> items = std.array::create::<i32>();
            std.array::push(&items, 7);
            std.array::push(&items, 8);
            storage_total_6.value = 0;
            for (const i32* x in &items) { storage_total_6.value += *x; }
            if (storage_total_6.value != 15) { throw TestAssertionFailed {.code = 9}; }
            for (i32* x in &items) { *x += 1; }
            storage_total_6.value = 0;
            for (i32 x in &items) { storage_total_6.value += x; }
            if (storage_total_6.value != 17) { throw TestAssertionFailed {.code = 10}; }

            list<i32> chain = std.list::create::<i32>();
            i32* first_node = std.list::push_back(&chain, 3);
            test_observe(&first_node);
            i32* second_node = std.list::push_back(&chain, 4);
            test_observe(&second_node);
            storage_total_6.value = 0;
            for (const i32* x in &chain) { storage_total_6.value += *x; }
            if (storage_total_6.value != 7) { throw TestAssertionFailed {.code = 13}; }

            dict<i32, i32> table = std.dict::create::<i32, i32>();
            o<i32> old_first = std.dict::insert(&table, 1, 10);
            old_first as void;
            o<i32> old_second = std.dict::insert(&table, 2, 20);
            old_second as void;
            storage_total_6.value = 0;
            for (auto entry in &table) { storage_total_6.value += *(entry.key) + *(entry.value); }
            if (storage_total_6.value != 33) { throw TestAssertionFailed {.code = 16}; }
        } catch (std.array::push_error<i32> push_failure) {
            push_failure as void;
            throw TestAssertionFailed {.code = 20};
        } catch (std.list::push_error<i32> list_failure) {
            list_failure as void;
            throw TestAssertionFailed {.code = 21};
        } catch (std.dict::insert_error<i32, i32> insert_failure) {
            insert_failure as void;
            throw TestAssertionFailed {.code = 22};
        }

        Counter counter = Counter { .next_value = 0, .limit = 4 };
        i32 total_7 = 0;
        for (i32 v in &counter) { total_7 += v; }
        if (total_7 != 6) { throw TestAssertionFailed {.code = 17}; }

        Counter other = Counter { .next_value = 1, .limit = 3 };
        i32 total_8 = 0;
        for (i32 v in move other) { total_8 += v; }
        if (total_8 != 3) { throw TestAssertionFailed {.code = 18}; }

        Counter third = Counter { .next_value = 0, .limit = 5 };
        i32 counted = count_items(&third);
        if (counted != 5) { throw TestAssertionFailed {.code = 19}; }
        return 0;
    } catch (TestAssertionFailed failure) { return failure.code; }
}

// Assertion failures unwind pending test values before reporting their exit code.
error TestAssertionFailed { i32 code; };
