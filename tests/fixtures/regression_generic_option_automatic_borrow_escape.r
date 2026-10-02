module regression.generic_option_automatic_borrow_escape;

@generic<T>
o<const T*> leak(T value) {
    return o::some(&value);
}

i32 main() {
    o<const i32*> escaped = leak(7);
    return 0;
}
