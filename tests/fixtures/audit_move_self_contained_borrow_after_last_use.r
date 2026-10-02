module audit.move_self_contained_borrow_after_last_use;

struct Holder {
    own i32* owner;
    const i32* alias;
};

i32 main() {
    i32 initial = 0;
    own i32* owner = new i32(7);
    Holder first = Holder {
        .owner = move owner,
        .alias = &initial,
    };
    first.alias = &*first.owner;
    i32 observed = *first.alias;
    Holder second = move first;
    second.owner = new i32(11);
    return observed - 7;
}
