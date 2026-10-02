module test.regression.finally_reaching_borrow_drop_uaf;

protected i32 probe(bool select_second) {
    own i32* first = new i32(61);
    own i32* second = new i32(67);
    const i32* selected = &*first;
    try {
        if (select_second == true) {
            selected = &*second;
        }
    } finally {
        drop second;
    }
    i32 result = *selected;
    drop first;
    return result;
}

i32 main() {
    i32 result = probe(false);
    i32 chosen = result == 61 ? 0 : 1;
    return chosen;
}
