module test.semantic.variadic_argument_mismatch;

/* R-FUNC-0018: every packed argument has the element type of the variadic parameter. */
i32 sum(i32... values) {
    i32 total = 0;
    for (i32 v in &values) { total += v; }
    return total;
}

i32 main() {
    i32 result = sum(1, true);
    return result;
}
