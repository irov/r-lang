module test.regression.branch_disjoint_borrow;

void mutate_or_read(bool mutate)
    throws std.alloc::alloc_error, std.array::push_error<u8> {
    array<u8> values = std.array::with_capacity::<u8>(1);
    std.array::push(&values, 1);
    {
        const u8[] view = std.array::as_slice(&values);
        if (mutate == true) {
            std.array::push(&values, 2);
        } else {
            len(view) as void;
        }
    }
}
