module audit.aggregate_slice_field;

struct View {
    const u8[] data;
};

i32 main() {
    u8[1] storage = {1};
    View view = { .data = &storage };
    usize length = len(view.data);
    i32 selected = length == 1 ? 0 : 1;
    return selected;
}
