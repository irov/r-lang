module test.codegen.async_mir_slice_bounds;

struct Scratch {
    u8[4] bytes;
};

async i32 main() {
    own Scratch* scratch = new Scratch{};
    i32 lower = 3;
    i32 upper = 2;
    const u8[] invalid = scratch->bytes[lower..upper];
    usize length = len(invalid);
    length as void;
    return 0;
}
