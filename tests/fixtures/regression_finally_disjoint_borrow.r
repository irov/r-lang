module test.regression.finally_disjoint_borrow;

protected i32 probe(bool leave) {
    own i32* first = new i32(31);
    own i32* second = new i32(37);
    const i32* selected = &*first;
    try {
        if (leave == true) {
            selected = &*second;
            return 0;
        }
    } finally {
        drop second;
    }
    i32 result = *selected;
    drop first;
    i32 chosen = result == 31 ? 0 : 1;
    return chosen;
}

i32 main() {
    i32 result = probe(false);
    return result;
}
