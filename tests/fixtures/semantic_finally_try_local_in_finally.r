module test.semantic.finally_try_local_in_finally;

protected void invalid_scope() {
    try {
        i32 hidden = 1;
    } finally {
        hidden as void;
    }
}
