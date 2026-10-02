module test.codegen.async_array_index;

/* R-EXPR-0021 in an async function: array<T> elements are indexed through the frame, before
   and after a suspension. */

async i32 main() {
    array<i32> values = std.array::create::<i32>();
    try {
        std.array::push(&values, 3);
        std.array::push(&values, 4);
    } catch (std.array::push_error<i32> failure) {
        move failure as void;
        return 1;
    }
    values[1] += 100;
    std.time::duration pause = std.time::duration_from_seconds(0i64);
    await pause.sleep_for();
    values[0] *= 2;
    if (values[0] + values[1] != 6 + 104) {
        return 2;
    }
    return 0;
}
