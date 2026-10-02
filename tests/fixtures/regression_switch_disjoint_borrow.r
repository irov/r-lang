module regression.switch_disjoint_borrow;

protected void append(array<u8>* values)
    throws std.array::push_error<u8> {
    std.array::push(values, 2);
}

void choose(i32 selected)
    throws std.alloc::alloc_error, std.array::push_error<u8> {
    array<u8> values = std.array::with_capacity::<u8>(1);
    std.array::push(&values, 1);
    const u8[] view = std.array::as_slice(&values);
    switch (selected) {
    case 0:
        append(&values);
        break;
    default:
        len(view) as void;
        break;
    }
}
