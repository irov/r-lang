module regression.loop_reassigned_borrow_owner_lifetime;

i32 main() {
    own i32* first = new i32(59);
    own i32* second = new i32(61);
    const i32* selected = &*first;
    i32 index = 0;
    while (index < 1) {
        selected = &*second;
        index += 1;
    }
    i32 result = *selected - 61;
    drop second;
    drop first;
    return result;
}
