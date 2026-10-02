module test.regression.throw_catch_borrow_outer_local;

error BorrowError {
    const i32* value;
};

i32 main() {
    i32 local = 41;
    try {
        throw BorrowError { .value = &local };
    } catch (BorrowError failure) {
        return *(failure.value) - 41;
    }
}
