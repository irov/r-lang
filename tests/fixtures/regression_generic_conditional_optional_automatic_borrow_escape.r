module regression.generic_conditional_optional_automatic_borrow_escape;

@generic<T>
o<const T*> leak(T value, bool present) {
    o<const T*> selected = (present == true) ? o::some(&value) : o::none;
    return selected;
}

i32 main() {
    o<const i32*> escaped = leak(7, true);
    return 0;
}
