module test.regression.catch_call_borrow_drop_uaf;

error BorrowError {
    const i32* value;
};

protected void fail(const i32* value) throws BorrowError {
    throw BorrowError { .value = value };
}

i32 main() {
    own i32* owner = new i32(41);
    try {
        fail(&*owner);
        return 1;
    } catch (BorrowError failure) {
        drop owner;
        return *(failure.value) - 41;
    }
}
