module test.regression.return_future_borrow_use_uaf;

protected i32 probe(bool leave) {
    own i32* first = new i32(83);
    own i32* second = new i32(89);
    const i32* selected = &*first;
    if (leave == true) {
        selected = &*second;
        drop second;
        i32 invalid = *selected;
        drop first;
        return invalid;
    }
    drop second;
    drop first;
    return 0;
}

i32 main() {
    i32 result = probe(false);
    return result;
}
