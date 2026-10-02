module codegen.slice_api;

const usize WIDTH = 3;

u8 middle(const u8[] bytes) {
    usize size = len(bytes);
    if (size == WIDTH) {
        return bytes[1];
    } else {
        return 0;
    }
}
