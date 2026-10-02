module test.regression.loop_conditional_overwrite_before_drop_uaf;

i32 main() {
    own i32* first = new i32(71);
    own i32* second = new i32(73);
    const i32* selected = &*first;
    i32 index = 0;
    while (index < 2) {
        if (index == 0) {
            selected = &*first;
        }
        drop second;
        second = new i32(79);
        selected = &*second;
        index += 1;
    }
    i32 result = *selected;
    drop second;
    drop first;
    return result;
}
