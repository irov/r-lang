module regression.generic_direct_automatic_borrow_escape;

@generic<T>
const T* leak(T value) {
    return &value;
}

i32 main() {
    const i32* escaped = leak(7);
    return *escaped;
}
