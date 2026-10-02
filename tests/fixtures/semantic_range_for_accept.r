module test.semantic.range_for_accept;

/* R-STMT-0014, R-EXPR-0029: every range-for form and membership test in condition and
   value positions. */
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

i32 main() {
    i32 total = 0;
    i32[3] fixed = {1, 2, 3};
    for (i32 i in 0..3) {
        if (i == 1) { continue; }
        total += i;
    }
    for (const i32* x in &fixed) { total += *x; }
    for (i32* x in &fixed) { *x += 1; }
    Counter counter = Counter { .next_value = 0, .limit = 2 };
    for (i32 v in &counter) { if (v == 1) { break; } total += v; }
    i32 probe = 0;
    while (probe in 0..3) { probe += 1; }
    if (total not in 0..100) { return 1; }
    return probe - 3;
}
