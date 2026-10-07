module test.semantic.core_bits_count;

/* R-LIB-0027: the count of a rotation is a u32. */
u32 rotate(u32 value, u64 count) {
    return core::rotate_left_u32(value, 2u64);
}

i32 main() {
    return 0;
}
