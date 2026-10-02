module test.regression.loop_carried_borrow_overwritten_before_drop;

i32 main() {
    own i32* first = new i32(1);
    own i32* second = new i32(2);
    const i32* selected = &*first;
    i32 index = 0;
    while (index < 2) {
        selected = &*first;
        drop second;
        second = new i32(3);
        selected = &*second;
        index += 1;
    }
    i32 result = *selected;
    drop second;
    drop first;
    i32 chosen = result == 3 ? 0 : 1;
    return chosen;
}
