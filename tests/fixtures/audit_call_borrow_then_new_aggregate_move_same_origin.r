module audit.call_borrow_then_new_aggregate_move_same_origin;

struct Holder {
    own i32* value;
};

i32 observe(const i32* borrowed, own Holder* consumed) {
    drop consumed;
    return *borrowed;
}

i32 main() {
    own i32* owner = new i32(17);
    i32 result = observe(&*owner, new Holder { .value = move owner });
    i32 selected = result == 17 ? 0 : 1;
    return selected;
}
