module test.semantic.borrow_fields_accept;

/* R-BORROW-0018: an aggregate carrying a borrow is an ordinary parameter, receiver and result
   of a synchronous R function. */
struct Holder {
    const i32* value;
};

i32 Holder::get(const Holder* this) {
    i32 v = *(this->value);
    return v;
}

const i32* Holder::view(const Holder* this) {
    return this->value;
}

Holder wrap(const i32* value) {
    return Holder { .value = value };
}

i32 read_holder(Holder holder) {
    i32 v = *(holder.value);
    return v;
}

i32 main() {
    i32 x = 4;
    Holder h = wrap(&x);
    i32 a = h.get();
    const i32* p = h.view();
    i32 b = read_holder(h);
    return a + *p + b - 12;
}
