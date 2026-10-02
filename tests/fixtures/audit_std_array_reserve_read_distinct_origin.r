module test.audit.std_array_reserve_read_distinct_origin;

i32 main() {
    try {
        array<i32> target = std.array::create::<i32>();
        array<i32> source = std.array::with_capacity::<i32>(3);
        std.array::reserve(&target, std.array::capacity(&source));
        if (std.array::capacity(&target) < 3 || std.array::capacity(&source) < 3) {
            return 1;
        }
        return 0;
    } catch (std.alloc::alloc_error failure) {
        return 2;
    }
}
