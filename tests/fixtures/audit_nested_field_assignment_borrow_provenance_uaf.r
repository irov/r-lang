module audit.nested_field_assignment_borrow_provenance_uaf;

struct Inner {
    own i32* owner;
    const i32* alias;
};

struct Outer {
    Inner inner;
};

i32 main() {
    i32 initial = 0;
    own i32* owner = new i32(7);
    Inner inner = Inner { .owner = move owner, .alias = &initial };
    Outer outer = Outer { .inner = move inner };
    outer.inner.alias = &*outer.inner.owner;
    outer.inner.owner = new i32(9);
    return *outer.inner.alias;
}
