module test.semantic.finally_continue_escape;

protected void invalid_continue() {
    while (true) {
        try {
            ;
        } finally {
            continue;
        }
    }
}
