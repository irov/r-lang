module audit.aggregate_initializer_borrow_then_move_same_origin;

struct Coupled {
    const i32* borrowed;
    own i32* owner;
};

i32 main() {
    own i32* owner = new i32(17);
    Coupled coupled = Coupled {
        .borrowed = &*owner,
        .owner = move owner,
    };
    own i32* replacement = new i32(23);
    coupled.owner = move replacement;
    return *(coupled.borrowed);
}
