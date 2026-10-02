module audit.std_bytes_equal_call;

i32 main() {
    u8[1] left = {1};
    u8[1] right = {1};
    const u8[] left_view = &left;
    const u8[] right_view = &right;
    bool equal = std.bytes::equal(left_view, right_view);
    if (equal == true) {
        return 0;
    }
    return 1;
}
