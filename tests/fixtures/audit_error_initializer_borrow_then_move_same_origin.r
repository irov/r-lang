module audit.error_initializer_borrow_then_move_same_origin;

error CoupledError {
    const i32* borrowed;
    own i32* owner;
};

i32 main() {
    own i32* owner = new i32(17);
    try {
        throw CoupledError {
            .borrowed = &*owner,
            .owner = move owner,
        };
    } catch (CoupledError failure) {
        own i32* replacement = new i32(23);
        failure.owner = move replacement;
        return *(failure.borrowed);
    }
}
