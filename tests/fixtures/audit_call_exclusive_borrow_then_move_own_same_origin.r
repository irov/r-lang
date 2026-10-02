module test.audit.call_exclusive_borrow_then_move_own_same_origin;

i32 observe(i32* borrowed, own i32* owner) {
    drop owner;
    return *borrowed;
}

i32 main() {
    own i32* owner = new i32(17);
    i32 result = observe(&*owner, move owner);
    i32 selected = result == 17 ? 0 : 1;
    return selected;
}
