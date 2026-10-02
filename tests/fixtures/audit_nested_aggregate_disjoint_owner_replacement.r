module audit.nested_aggregate_disjoint_owner_replacement;

struct Holder {
    own i32* owner;
    const i32* alias;
};

struct Pair {
    Holder left;
    Holder right;
};

i32 main() {
    i32 left_initial = 0;
    i32 right_initial = 0;
    own i32* left_owner = new i32(7);
    own i32* right_owner = new i32(13);
    Holder left = Holder {
        .owner = move left_owner,
        .alias = &left_initial,
    };
    Holder right = Holder {
        .owner = move right_owner,
        .alias = &right_initial,
    };
    left.alias = &*left.owner;
    Pair pair = Pair {
        .left = move left,
        .right = move right,
    };
    pair.right.owner = new i32(17);
    return *pair.left.alias - 7;
}
