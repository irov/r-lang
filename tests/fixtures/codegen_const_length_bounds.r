module test.codegen.const_length_bounds;
@generic<const usize N>
async usize row_width(u8[2usize][N] rows, usize index) {
    usize count = len(rows[index]);
    return count;
}
async i32 main() {
    u8[2usize][4usize] rows = {};
    try {
        usize count = await row_width(rows, 2usize);
        count as void;
        return 0;
    } catch (std.async::start_error failure) { return 1; }
}
