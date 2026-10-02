module audit.call_borrow_then_conditional_move_same_origin;

i32 observe(const i32* borrowed, own i32* consumed) {
    drop consumed;
    return *borrowed;
}

i32 main() {
    own i32* owner = new i32(17);
    bool select_owner = true;
    i32 result = observe(&*owner, (select_owner == true) ? move owner : move owner);
    i32 selected = result == 17 ? 0 : 1;
    return selected;
}
