module audit.std_bytes_compare_call;

i32 main() {
    u8[2] left = {1, 2};
    u8[2] right = {1, 3};
    const u8[] left_view = &left;
    const u8[] right_view = &right;
    i32 selected = std.bytes::compare(left_view, right_view) < 0 ? 0 : 1;
    return selected;
}
