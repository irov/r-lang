module codegen.new_expression;

// Retain values whose purpose here is type or lifetime coverage.
@generic<T> @noalloc @nonblocking
protected void test_observe(const T* value) { value as void; }


struct Item {
    i32 value;
};

protected own i32* allocate_scalar() {
    return new i32(7);
}

protected arc Item allocate_arc() {
    return new arc Item {
        .value = 11,
    };
}

protected rc Item allocate_rc() {
    Item value = {
        .value = 13,
    };
    return new rc Item(value);
}

i32 main() {
    own i32* scalar = allocate_scalar();
    test_observe(&scalar);
    arc Item shared = allocate_arc();
    test_observe(&shared);
    rc Item local = allocate_rc();
    test_observe(&local);
    return 0;
}
