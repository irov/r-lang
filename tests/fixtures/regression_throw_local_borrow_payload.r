module test.regression.throw_local_borrow_payload;

error BorrowError {
    const i32* value;
};

protected void fail() throws BorrowError {
    i32 local = 41;
    throw BorrowError { .value = &local };
}

i32 main() {
    return 0;
}
