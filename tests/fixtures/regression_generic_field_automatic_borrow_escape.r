module regression.generic_field_automatic_borrow_escape;

@generic<T>
struct Box {
    T value;
};

@generic<T>
const T* leak(T value) {
    Box<T> box = Box<T> { .value = move value };
    return &box.value;
}

i32 main() {
    const i32* escaped = leak(7);
    return 0;
}
