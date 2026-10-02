module test.audit.std_array_reserve_read_same_origin;

i32 main() {
    try {
        array<i32> values = std.array::create::<i32>();
        std.array::reserve(&values, std.array::capacity(&values));
        return 0;
    } catch (std.alloc::alloc_error failure) {
        return 2;
    }
}
