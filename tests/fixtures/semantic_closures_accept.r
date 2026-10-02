module test.semantic.closures_accept;

/* R-FUNC-0015 through R-FUNC-0017, R-TYPE-0044, R-NAME-0011. */
@generic<F: fn(i32) -> i32>
i32 apply(const F* f, i32 value) {
    i32 result = f(value);
    return result;
}

i32 main() {
    i32 offset = 1;
    fn i32 add(i32 x) { return x + offset; }
    auto value = add(2);
    i32 applied = apply(&add, value);
    return applied;
}
