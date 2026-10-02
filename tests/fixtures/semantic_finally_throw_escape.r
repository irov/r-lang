module test.semantic.finally_throw_escape;

error cleanup_error {
    i32 code;
};

protected void invalid_throw() throws cleanup_error {
    try {
        ;
    } finally {
        throw {
            .code = 1,
        };
    }
}
