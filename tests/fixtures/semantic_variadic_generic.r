module test.semantic.variadic_generic;

/* R-FUNC-0018: a variadic parameter belongs to an ordinary, non-generic function. */
@generic<T: copy>
usize count(T... values) {
    usize n = len(values);
    return n;
}

i32 main() {
    usize n = count(1, 2);
    n as void;
    return 0;
}
