module audit.conditional_move_self_contained_borrow_uaf;

struct Holder {
    own i32* owner;
    const i32* alias;
};

i32 main() {
    i32 first_initial = 0;
    i32 second_initial = 0;
    own i32* first_owner = new i32(7);
    own i32* second_owner = new i32(13);
    Holder first = Holder {
        .owner = move first_owner,
        .alias = &first_initial,
    };
    Holder alternative = Holder {
        .owner = move second_owner,
        .alias = &second_initial,
    };
    first.alias = &*first.owner;
    Holder selected = true ? move first : move alternative;
    selected.owner = new i32(11);
    return *selected.alias;
}
