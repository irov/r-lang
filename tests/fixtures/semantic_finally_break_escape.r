module test.semantic.finally_break_escape;

protected void invalid_break() {
    while (true) {
        try {
            ;
        } finally {
            break;
        }
    }
}
