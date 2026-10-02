module regression.generic_constant_dead_automatic_borrow;

@generic<T>
o<const T*> absent(T value) {
    o<const T*> selected = false ? o::some(&value) : o::none;
    return selected;
}

i32 main() {
    o<const i32*> result = absent(7);
    result as void;
    return 0;
}
