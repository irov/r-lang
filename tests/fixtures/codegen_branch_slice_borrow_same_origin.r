module test.codegen.branch_slice_borrow_same_origin;

const i32* choose(bool repeat, const i32[] source) {
    const i32[] selected = source;
    if (repeat == true) {
        selected = source;
    }
    const i32* result = &(selected[0]);
    return result;
}

i32 main() {
    i32[1] values = {23};
    const i32[] source = &values;
    const i32* selected = choose(true, source);
    return *selected - 23;
}
