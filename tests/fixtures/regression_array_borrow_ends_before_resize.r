module regression.array_borrow_ends_before_resize;

i32 main() {
    try {
        array<i32> values = std.array::with_capacity::<i32>(1);
        std.array::push(&values, 17);
        const i32[] view = std.array::as_slice(&values);
        i32 first = view[0];
        std.array::push(&values, 29);
        return first - 17;
    } catch (std.array::push_error<i32> error) {
        error as void;
        return 2;
    } catch (std.alloc::alloc_error error) {
        error as void;
        return 1;
    }
}
