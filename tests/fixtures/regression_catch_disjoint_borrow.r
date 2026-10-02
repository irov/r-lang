module test.regression.catch_disjoint_borrow;

error Stop {};

protected i32 probe(bool leave) {
    own i32* first = new i32(97);
    own i32* second = new i32(101);
    const i32* selected = &*first;
    try {
        if (leave == true) {
            selected = &*second;
            throw Stop {};
        }
    } catch (Stop failure) {
        drop second;
        failure as void;
        return 0;
    }
    i32 result = *selected;
    drop second;
    drop first;
    i32 chosen = result == 97 ? 0 : 1;
    return chosen;
}

i32 main() {
    i32 result = probe(false);
    return result;
}
