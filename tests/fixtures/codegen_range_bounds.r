module test.codegen.range_bounds;

i32 main() {
    u8[2] values = {1, 2};
    i32 lower = -1;
    i32 upper = 1;
    const u8[] invalid = values[lower..upper];
    return 0;
}
