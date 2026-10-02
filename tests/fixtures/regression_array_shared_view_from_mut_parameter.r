module regression.array_shared_view_from_mut_parameter;

usize length(array<u8>* values) {
    const u8[] view = std.array::as_slice(values);
    usize count = len(view);
    return count;
}

i32 main() {
    array<u8> values = {};
    usize count = length(&values);
    return count as i32;
}
