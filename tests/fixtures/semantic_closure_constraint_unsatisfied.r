module test.semantic.closure_constraint_unsatisfied;

/* R-TYPE-0044: a closure proves a callable constraint only with the exact signature. */
@generic<F: fn(i32) -> i32>
i32 apply(const F* f, i32 value) {
    i32 result = f(value);
    return result;
}

i32 main() {
    fn u64 wide(i32 x) { return x as u64; }
    i32 applied = apply(&wide, 2);
    return applied;
}
