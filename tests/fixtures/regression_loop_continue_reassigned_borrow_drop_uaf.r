module regression.loop_continue_reassigned_borrow_drop_uaf;

i32 main() {
    own i32* first = new i32(59);
    own i32* second = new i32(61);
    const i32* selected = &*first;
    i32 index = 0;
    while (index < 1) {
        selected = &*second;
        index += 1;
        continue;
    }
    drop second;
    i32 result = *selected - 61;
    drop first;
    return result;
}
