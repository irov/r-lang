module test.audit.std_bytes_copy_disjoint_ranges;

i32 main() {
    try {
        u8[8] value = {0, 1, 2, 3, 9, 9, 9, 9};
        usize copied = std.bytes::copy(value[4..8], value[0..4]);
        if (copied != 4 || value[4] != 0 || value[5] != 1 || value[6] != 2 ||
            value[7] != 3) {
            return 1;
        }
        return 0;
    } catch (std.bytes::bytes_error failure) {
        return 2;
    }
}
