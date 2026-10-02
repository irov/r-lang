module test.codegen.range_slice;

i32 main() {
    u8[5] values = {1, 2, 3, 4, 5};
    i32 lower = 1;
    i32 upper = 4;
    u8[] middle = values[lower..upper];
    middle[0] = 9;
    const u8[] whole_middle = middle;
    const u8[] tail = middle[1..3];
    const u8[] empty = tail[2..2];
    i32 signed_index = 1;
    if (values[1] != 9) {
        return 1;
    }
    if (len(middle) != 3) {
        return 2;
    }
    if ((len(whole_middle) != 3) || (whole_middle[0] != 9)) {
        return 3;
    }
    if (tail[0] != 3) {
        return 4;
    }
    if (len(empty) != 0) {
        return 5;
    }
    if (values[signed_index] != 9) {
        return 6;
    }
    return 0;
}
