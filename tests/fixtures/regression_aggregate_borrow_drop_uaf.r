module test.regression.aggregate_borrow_drop_uaf;

struct View {
    const i32* value;
};

i32 main() {
    own i32* owner = new i32(41);
    View view = View { .value = &*owner };
    drop owner;
    return *(view.value) - 41;
}
