module test.regression.throw_parameter_borrow_payload;

error BorrowError {
    const i32* value;
};

protected void fail(const i32* input) throws BorrowError {
    throw BorrowError { .value = input };
}

protected void relay(const i32* input) throws BorrowError {
    fail(input);
}

i32 main() {
    i32 local = 41;
    try {
        relay(&local);
        return 90;
    } catch (BorrowError failure) {
        return *(failure.value) - 41;
    }
}
