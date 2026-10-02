module test.semantic.qualified_constraint_accept;

/* R-TYPE-0043, R-TYPE-0044: a qualified trait constraint of the current module, and a callable
   constraint whose result type follows from the closure argument. */
trait Shown {
    i32 show(const Self* this);
};

impl Shown for i32 {
    i32 show(const i32* this) {
        return *this;
    }
};

@generic<T: test.semantic.qualified_constraint_accept::Shown>
i32 describe(const T* value) {
    i32 r = value->show();
    return r;
}

@generic<U, F: fn(i32) -> U>
U apply(const F* function, i32 x) {
    U r = function(x);
    return move r;
}

i32 main() {
    i32 v = 5;
    i32 a = describe(&v);
    fn bool positive(i32 x) { return x > 0; }
    bool b = apply(&positive, a);
    if (b == false) { return 1; }
    return a - 5;
}
