module test.regression.loop_carried_borrow_valid;

i32 main() {
    own i32* first = new i32(59);
    own i32* second = new i32(61);
    const i32* selected = &*first;
    i32 index = 0;
    i32 sum = 0;
    while (index < 2) {
        sum += *selected;
        selected = &*second;
        index += 1;
    }
    drop second;
    drop first;
    i32 chosen = sum == 120 ? 0 : 1;
    return chosen;
}
