module test.regression.return_path_disjoint_borrow;

protected i32 probe(bool leave) {
    own i32* first = new i32(47);
    own i32* second = new i32(53);
    const i32* selected = &*first;
    if (leave == true) {
        selected = &*second;
        drop second;
        return 0;
    }
    i32 result = *selected;
    drop second;
    drop first;
    i32 chosen = result == 47 ? 0 : 1;
    return chosen;
}

i32 main() {
    i32 result = probe(false);
    return result;
}
