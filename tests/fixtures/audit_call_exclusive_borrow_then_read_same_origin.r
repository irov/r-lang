module audit.call_exclusive_borrow_then_read_same_origin;

i32 observe(i32* borrowed, i32 snapshot) {
    snapshot as void;
    *borrowed = 19;
    return *borrowed;
}

i32 main() {
    i32 value = 17;
    i32 result = observe(&value, value);
    i32 selected = result == 19 ? 0 : 1;
    return selected;
}
