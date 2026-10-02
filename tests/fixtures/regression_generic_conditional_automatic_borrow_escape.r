module regression.generic_conditional_automatic_borrow_escape;

@generic<T>
const T* leak(T value, bool first) {
    const T* selected = (first == true) ? &value : &value;
    return selected;
}

i32 main() {
    const i32* escaped = leak(7, true);
    return *escaped;
}
