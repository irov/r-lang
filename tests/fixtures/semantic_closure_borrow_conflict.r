module test.semantic.closure_borrow_conflict;

/* R-FUNC-0016, R-BORROW-0003: an exclusive capture excludes other access while the closure lives. */
i32 main() {
    i32 total = 0;
    fn void bump(i32 delta) { total += delta; }
    total = 5;
    bump(1);
    return total;
}
