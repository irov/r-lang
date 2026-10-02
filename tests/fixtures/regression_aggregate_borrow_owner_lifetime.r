module test.regression.aggregate_borrow_owner_lifetime;

struct View {
    const i32* value;
};

i32 main() {
    own i32* owner = new i32(41);
    View view = View { .value = &*owner };
    i32 result = *(view.value) - 41;
    drop owner;
    return result;
}
