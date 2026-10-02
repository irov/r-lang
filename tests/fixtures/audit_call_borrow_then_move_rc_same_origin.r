module test.audit.call_borrow_then_move_rc_same_origin;

struct Item {
    i32 value;
};

i32 observe(const Item* borrowed, rc Item owner) {
    drop owner;
    return borrowed->value;
}

i32 main() {
    rc Item owner = new rc Item { .value = 17 };
    i32 result = observe(&*owner, move owner);
    i32 selected = result == 17 ? 0 : 1;
    return selected;
}
