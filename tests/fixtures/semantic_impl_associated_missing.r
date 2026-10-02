module test.semantic.impl_associated_missing;

/* R-TYPE-0045: an implementation binds every associated type its trait declares. */
struct Counter {
    i32 next_value;
};

impl core::Iterator for Counter {
    o<i32> next(Counter* this) { return o::none; }
};

i32 main() {
    return 0;
}
