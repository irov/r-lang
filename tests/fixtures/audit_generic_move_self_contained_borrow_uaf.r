module audit.generic_move_self_contained_borrow_uaf;

struct Holder {
    own i32* owner;
    const i32* alias;
};

@generic<T>
T identity(T value) {
    return move value;
}

i32 main() {
    i32 initial = 0;
    own i32* owner = new i32(7);
    Holder first = Holder {
        .owner = move owner,
        .alias = &initial,
    };
    first.alias = &*first.owner;
    Holder second = identity(move first);
    second.owner = new i32(11);
    return *second.alias;
}
