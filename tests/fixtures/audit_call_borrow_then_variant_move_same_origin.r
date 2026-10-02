module audit.call_borrow_then_variant_move_same_origin;

enum Holder {
    Owned(own i32*),
};

i32 observe(const i32* borrowed, Holder consumed) {
    drop consumed;
    return *borrowed;
}

i32 main() {
    own i32* owner = new i32(17);
    i32 result = observe(&*owner, Holder::Owned(move owner));
    i32 selected = result == 17 ? 0 : 1;
    return selected;
}
