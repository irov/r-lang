module test.semantic.catch_local_after;

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
    }
    error as void;
}
