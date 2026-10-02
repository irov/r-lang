module regression.conditional_disjoint_borrow;

protected usize append(array<u8>* values)
    throws std.array::push_error<u8> {
    std.array::push(values, 2);
    return 1;
}

void choose(bool mutate)
    throws std.alloc::alloc_error, std.array::push_error<u8> {
    array<u8> values = std.array::with_capacity::<u8>(1);
    std.array::push(&values, 1);
    const u8[] view = std.array::as_slice(&values);
    usize selected = mutate == true ? append(&values) : len(view);
    selected as void;
}
