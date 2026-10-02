module test.semantic.finally_catch_local_in_finally;

error work_error {
    i32 code;
};

protected void fail() throws work_error {
    throw {
        .code = 1,
    };
}

protected void invalid_scope() {
    try {
        fail();
    } catch (work_error error) {
        error as void;
    } finally {
        error as void;
    }
}
