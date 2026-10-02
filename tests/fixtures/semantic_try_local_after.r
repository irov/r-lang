module test.semantic.try_local_after;

protected void invalid_scope() {
    try {
        i32 hidden = 1;
    } finally {
        ;
    }
    hidden as void;
}
