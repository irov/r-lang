module audit.call_borrow_then_nested_move_distinct_origin;

struct Holder {
    own i32* value;
};

enum Variant {
    Owned(own i32*),
};

i32 observe_holder(const i32* borrowed, Holder consumed) {
    drop consumed;
    return *borrowed;
}

i32 observe_variant(const i32* borrowed, Variant consumed) {
    drop consumed;
    return *borrowed;
}

i32 observe_pointer(const i32* borrowed, own Holder* consumed) {
    drop consumed;
    return *borrowed;
}

i32 observe_owner(const i32* borrowed, own i32* consumed) {
    drop consumed;
    return *borrowed;
}

i32 main() {
    own i32* borrowed_a = new i32(17);
    own i32* moved_a = new i32(29);
    i32 aggregate_result =
        observe_holder(&*borrowed_a, Holder { .value = move moved_a });

    own i32* borrowed_b = new i32(31);
    own i32* moved_b = new i32(37);
    i32 variant_result =
        observe_variant(&*borrowed_b, Variant::Owned(move moved_b));
    variant_result as void;

    own i32* borrowed_c = new i32(41);
    own i32* moved_c = new i32(43);
    i32 new_result =
        observe_pointer(&*borrowed_c, new Holder { .value = move moved_c });
    new_result as void;

    own i32* borrowed_d = new i32(47);
    own i32* moved_d = new i32(53);
    bool select_owner = true;
    own i32* consumed_d = (select_owner == true) ? move moved_d : move moved_d;
    i32 conditional_result = observe_owner(&*borrowed_d, move consumed_d);
    conditional_result as void;

    i32 selected = aggregate_result == 17 && variant_result == 31 &&
                   new_result == 41 && conditional_result == 47
               ? 0
               : 1;
    return selected;
}
