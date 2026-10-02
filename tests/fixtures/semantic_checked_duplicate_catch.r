module test.semantic.checked_duplicate_catch;

error operation_error {
    i32 code;
};

protected void fail() throws operation_error {
    throw {
        .code = 1,
    };
}

protected void invalid() {
    try {
        fail();
    } catch (operation_error first) {
        first as void;
    } catch (operation_error second) {
        second as void;
    }
}
