module regression.array_alias_resize_uaf;

i32 main() {
    try {
        array<i32> values = std.array::with_capacity::<i32>(1);
        std.array::push(&values, 17);
        array<i32>* target = &values;
        const i32[] view = std.array::as_slice(target);
        std.array::push(target, 29);
        return view[0] - 17;
    } catch (std.array::push_error<i32> error) {
        error as void;
        return 2;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 1;
    }
}
