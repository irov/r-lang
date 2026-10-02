module test.codegen.async_array_index_bounds;

/* R-EXPR-0021 in an async function: an index past the live length causes the bounds panic. */
async i32 main() {
    array<i32> values = std.array::create::<i32>();
    try {
        std.array::push(&values, 1);
    } catch (std.array::push_error<i32> failure) {
        move failure as void;
        return 1;
    }
    std.time::duration pause = std.time::duration_from_seconds(0i64);
    await pause.sleep_for();
    usize index = len(values);
    values[index] = 3;
    return 0;
}
