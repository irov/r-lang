module regression.generic_branch_optional_automatic_borrow_escape;

@generic<T>
o<const T*> leak(T value, bool present) {
    o<const T*> selected = o::none;
    if (present == true) {
        selected = o::some(&value);
    }
    return selected;
}

i32 main() {
    o<const i32*> escaped = leak(7, true);
    return 0;
}
