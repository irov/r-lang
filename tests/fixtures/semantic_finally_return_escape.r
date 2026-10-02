module test.semantic.finally_return_escape;

protected void invalid_return() {
    try {
        ;
    } finally {
        return;
    }
}
