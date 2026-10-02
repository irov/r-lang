module test.codegen.borrow_fields;

/* R-BORROW-0018: aggregates that carry borrows or slices travel through signatures by value
   and through borrows; their hidden regions follow the arguments of every call. */
struct Holder {
    const i32* value;
};

struct Slot {
    i32* target;
};

struct Window {
    const i32[] items;
    usize start;
};

@generic<T: copy>
struct Pair {
    const T* left;
    const T* right;
};

@generic<F: fn(i32) -> i32>
struct Applier {
    const F* function;
};

i32 Holder::get(const Holder* this) {
    i32 v = *(this->value);
    return v;
}

const i32* Holder::view(const Holder* this) {
    return this->value;
}

i32 read_holder(Holder holder) {
    i32 v = *(holder.value);
    return v;
}

void Slot::bump(Slot* this) {
    *(this->target) += 1;
}

usize Window::remaining(const Window* this) {
    usize n = len(this->items) - this->start;
    return n;
}

const i32* Window::first(const Window* this) {
    return &this->items[this->start];
}

@generic<T: copy>
T Pair<T>::pick(const Pair<T>* this, bool first) {
    if (first == true) {
        T l = *(this->left);
        return l;
    }
    T r = *(this->right);
    return r;
}

@generic<T: copy>
Pair<T> make_pair(const T* left, const T* right) {
    return Pair<T> { .left = left, .right = right };
}

@generic<F: fn(i32) -> i32>
i32 Applier<F>::apply(const Applier<F>* this, i32 x) {
    i32 v = this->function(x);
    return v;
}

@generic<F: fn(i32) -> i32>
Applier<F> hold(const F* function) {
    return Applier<F> { .function = function };
}

i32 main() {
    i32 x = 4;
    i32 y = 9;
    Holder h = Holder { .value = &x };
    i32 a = h.get();
    if (a != 4) { return 1; }
    const i32* pv = h.view();
    i32 b = *pv;
    if (b != 4) { return 2; }
    i32 c = read_holder(h);
    if (c != 4) { return 3; }
    Slot s = Slot { .target = &y };
    s.bump();
    s.bump();
    i32[3] fixed = {1, 2, 3};
    Window w = Window { .items = &fixed, .start = 1usize };
    usize rem = w.remaining();
    if (rem != 2usize) { return 4; }
    const i32* f = w.first();
    if (*f != 2) { return 5; }
    Pair<i32> p = make_pair(&x, &y);
    i32 l = p.pick(true);
    if (l != 4) { return 6; }
    i32 r = p.pick(false);
    if (r != 11) { return 7; }
    fn i32 twice(i32 v) { return v * 2; }
    auto ap = hold(&twice);
    i32 d = ap.apply(21);
    if (d != 42) { return 8; }
    return 0;
}
