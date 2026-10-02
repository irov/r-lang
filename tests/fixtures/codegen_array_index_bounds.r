module test.codegen.array_index_bounds;

/* R-EXPR-0021: an array<T> index at or past the live length causes the bounds panic. */
i32 main() {
    array<i32> values = std.array::create::<i32>();
    try {
        std.array::push(&values, 1);
        std.array::push(&values, 2);
    } catch (std.array::push_error<i32> failure) {
        move failure as void;
        return 1;
    }
    usize index = len(values);
    return values[index];
}
