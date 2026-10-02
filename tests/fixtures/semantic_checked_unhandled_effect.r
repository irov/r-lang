module test.semantic.checked_unhandled_effect;

error operation_error {
    i32 code;
};

protected void fail() throws operation_error {
    throw {
        .code = 1,
    };
}

protected void invalid() {
    fail();
}
