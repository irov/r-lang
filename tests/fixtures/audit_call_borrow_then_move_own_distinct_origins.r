module test.audit.call_borrow_then_move_own_distinct_origins;

i32 observe(const i32* borrowed, own i32* other) {
    drop other;
    return *borrowed;
}

i32 main() {
    own i32* owner = new i32(17);
    own i32* other = new i32(23);
    i32 result = observe(&*owner, move other);
    drop owner;
    i32 selected = result == 17 ? 0 : 1;
    return selected;
}
