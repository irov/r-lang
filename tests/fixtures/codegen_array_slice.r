module test.codegen.array_slice;

usize const_length(const array<u8>* values) {
    const u8[] view = std.array::as_slice(values);
    usize length = len(view);
    return length;
}

usize mutable_length(array<u16>* values) {
    u16[] view = std.array::as_slice_mut(values);
    usize length = len(view);
    return length;
}

i32 main() {
    return 0;
}
