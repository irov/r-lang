module audit.nested_aggregate_move_after_last_use;

struct Holder {
    own i32* owner;
    const i32* alias;
};

struct Outer {
    Holder inner;
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
    Outer outer = Outer { .inner = move first };
    outer.inner.owner = new i32(11);
    return observed - 7;
}
