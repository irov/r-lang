module audit.aggregate_initializer_borrow_then_conditional_move_same_origin;

struct Coupled {
    const i32* borrowed;
    own i32* owner;
};

i32 main() {
    own i32* owner = new i32(17);
    bool select_owner = true;
    Coupled coupled = Coupled {
        .borrowed = &*owner,
        .owner = (select_owner == true) ? move owner : move owner,
    };
    return *(coupled.borrowed);
}
