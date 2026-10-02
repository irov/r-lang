module test.semantic.variadic_result_borrow;

/* R-FUNC-0018: the result of a call shall not borrow from the packed arguments, whose hidden
   array lives only for the call. */
const i32[] all(i32... values) {
    return values;
}

i32 main() {
    const i32[] kept = all(1, 2);
    usize n = len(kept);
    n as void;
    return 0;
}
