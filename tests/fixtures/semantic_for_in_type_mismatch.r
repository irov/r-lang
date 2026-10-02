module test.semantic.for_in_type_mismatch;

/* R-STMT-0014: the loop variable has the item type of the iterator. */
struct Counter {
    i32 next_value;
};

impl core::Iterator for Counter {
    type Item = i32;
    o<i32> next(Counter* this) { return o::none; }
};

i32 main() {
    Counter counter = Counter { .next_value = 0 };
    for (u8 v in &counter) { v as void; }
    return 0;
}
