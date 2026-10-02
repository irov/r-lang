module test.audit.std_bytes_copy_overlapping_ranges;

i32 main() {
    try {
        u8[8] value = {0, 1, 2, 3, 4, 5, 6, 7};
        usize copied = std.bytes::copy(value[1..8], value[0..7]);
        if (copied != 7) {
            return 1;
        }
        return 0;
    } catch (std.bytes::bytes_error failure) {
        return 2;
    }
}
