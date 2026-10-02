module test.semantic.for_in_move_required;

/* R-STMT-0014, R-OWN-0003: a named Move iterator is consumed with move or borrowed. */
struct Counter {
    own i32* cell;
};

impl core::Iterator for Counter {
    type Item = i32;
    o<i32> next(Counter* this) { return o::none; }
};

i32 main() {
    Counter counter = Counter { .cell = new i32(0) };
    for (i32 v in counter) { v as void; }
    return 0;
}
