module regression.generic_call_automatic_borrow_escape;

@generic<T>
const T* identity_ref(const T* pointer) {
    return pointer;
}

@generic<T>
const T* leak(T value) {
    const T* pointer = identity_ref(&value);
    return pointer;
}

void instantiate() {
    const i32* escaped = leak(7);
}

i32 main() {
    return 0;
}
