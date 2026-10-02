module test.semantic.borrow_field_escape;

/* R-BORROW-0007: an aggregate that carries a borrow of automatic storage does not outlive it. */
struct Holder {
    const i32* value;
};

Holder make() {
    i32 x = 1;
    Holder h = Holder { .value = &x };
    return h;
}

i32 main() {
    Holder h = make();
    return *(h.value);
}
