module audit.aggregate_initializer_borrow_then_nested_move_same_origin;

struct OwnerBox {
    own i32* owner;
};

struct Coupled {
    const i32* borrowed;
    OwnerBox box;
};

i32 main() {
    own i32* owner = new i32(17);
    Coupled coupled = Coupled {
        .borrowed = &*owner,
        .box = OwnerBox { .owner = move owner },
    };
    return *(coupled.borrowed);
}
