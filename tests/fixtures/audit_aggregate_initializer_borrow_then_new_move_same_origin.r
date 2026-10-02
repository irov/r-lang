module audit.aggregate_initializer_borrow_then_new_move_same_origin;

struct OwnerBox {
    own i32* owner;
};

struct Coupled {
    const i32* borrowed;
    own OwnerBox* box;
};

i32 main() {
    own i32* owner = new i32(17);
    Coupled coupled = Coupled {
        .borrowed = &*owner,
        .box = new OwnerBox { .owner = move owner },
    };
    return *(coupled.borrowed);
}
