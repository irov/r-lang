module audit.generic_aggregate_initializer_borrow_then_move_same_origin;

@generic<T: unborrowed>
struct Coupled {
    const T* borrowed;
    own T* owner;
};

i32 main() {
    own i32* owner = new i32(17);
    Coupled<i32> coupled = Coupled<i32> {
        .borrowed = &*owner,
        .owner = move owner,
    };
    own i32* replacement = new i32(23);
    coupled.owner = move replacement;
    return *(coupled.borrowed);
}
