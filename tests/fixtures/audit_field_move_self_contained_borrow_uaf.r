module audit.field_move_self_contained_borrow_uaf;

struct Holder {
    own i32* owner;
    const i32* alias;
};

struct Outer {
    Holder inner;
};

i32 main() {
    i32 initial = 0;
    i32 replacement_initial = 0;
    own i32* owner = new i32(7);
    own i32* replacement_owner = new i32(13);
    Holder first = Holder {
        .owner = move owner,
        .alias = &initial,
    };
    Holder replacement = Holder {
        .owner = move replacement_owner,
        .alias = &replacement_initial,
    };
    first.alias = &*first.owner;
    Outer outer = Outer { .inner = move replacement };
    outer.inner = move first;
    outer.inner.owner = new i32(11);
    return *outer.inner.alias;
}
