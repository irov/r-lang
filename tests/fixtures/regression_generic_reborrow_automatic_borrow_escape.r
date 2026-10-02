module regression.generic_reborrow_automatic_borrow_escape;

@generic<T>
const T* leak(T value) {
    const T* pointer = &value;
    return &*pointer;
}

i32 main() {
    const i32* escaped = leak(7);
    return *escaped;
}
