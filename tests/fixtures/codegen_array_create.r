module test.codegen.array_create;

i32 main() {
    try {
        array<i32> values = std.array::create::<i32>();
        if (len(values) != 0usize) {
            drop values;
            return 1;
        }
        std.array::push(&values, 29);
        const i32[] view = std.array::as_slice(&values);
        i32 result = len(view) == 1usize && view[0] == 29 ? 0 : 2;
        drop values;
        return result;
    } catch (std.array::push_error<i32> error) {
        error as void;
        return 3;
    }
}
