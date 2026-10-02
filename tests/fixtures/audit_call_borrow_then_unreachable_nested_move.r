module audit.call_borrow_then_unreachable_nested_move;

i32 observe(const i32* borrowed, own i32* consumed) {
    drop consumed;
    return *borrowed;
}

i32 main() {
    own i32* owner = new i32(17);
    own i32* other = new i32(23);
    own i32* consumed = false ? move owner : move other;
    i32 result = observe(&*owner, move consumed);
    i32 retained = *owner;
    drop owner;
    i32 selected = result == 17 && retained == 17 ? 0 : 1;
    return selected;
}
