module test.regression.catch_reaching_borrow_drop_uaf;

error Stop {};

i32 main() {
    own i32* first = new i32(103);
    own i32* second = new i32(107);
    const i32* selected = &*first;
    try {
        selected = &*second;
        throw Stop {};
    } catch (Stop failure) {
        drop second;
        failure as void;
    }
    i32 result = *selected;
    drop first;
    return result;
}
