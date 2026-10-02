module regression.folded_unreachable_loop_borrow;

i32 main() {
    try {
        array<u8> values = std.array::with_capacity::<u8>(1);
        std.array::push(&values, 1);
        const u8[] view = std.array::as_slice(&values);
        i32 iteration = 0;
        while (iteration < 2) {
            if (1 == 0) {
                len(view) as void;
            }
            std.array::push(&values, 2);
            iteration += 1;
        }
        if (len(values) != 3) {
            return 1;
        }
        return 0;
    } catch (std.array::push_error<u8> failure) {
        failure as void;
        return 2;
    } catch (std.alloc::alloc_error failure) {
        failure as void;
        return 3;
    }
}
