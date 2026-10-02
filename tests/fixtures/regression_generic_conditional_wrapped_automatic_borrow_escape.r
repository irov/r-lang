module regression.generic_conditional_wrapped_automatic_borrow_escape;

@generic<T>
o<const T*> leak(T value, bool first) {
    o<const T*> selected = (first == true) ? o::some(&value) : o::some(&value);
    return selected;
}

i32 main() {
    o<const i32*> escaped = leak(7, true);
    return 0;
}
