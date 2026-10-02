module profile.allocation_ok;

i32 main() {
    try {
        array<i32> values = std.array::with_capacity::<i32>(2usize);
        std.array::push(&values, 40);
        std.array::push(&values, 2);
        own i32* boxed = new i32(1);
        arc i32 shared = new arc i32(2);
        if (len(values) != 2usize) {
            return 5;
        }
    } catch (std.alloc::alloc_error failure) {
        return 3;
    } catch (std.array::push_error<i32> failure) {
        return 4;
    }
    return 0;
}
