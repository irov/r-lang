module test.semantic.finally_uncaught_call;

error cleanup_error {
    i32 code;
};

protected void fail() throws cleanup_error {
    throw {
        .code = 1,
    };
}

protected void invalid_call() {
    try {
        ;
    } finally {
        fail();
    }
}
