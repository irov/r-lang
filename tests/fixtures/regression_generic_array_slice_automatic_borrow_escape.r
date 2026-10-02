module regression.generic_array_slice_automatic_borrow_escape;

@generic<T: copy>
const T[] leak(T value) {
    T[1] values = { value };
    return values[0..1];
}

i32 main() {
    const i32[] escaped = leak(7);
    return 0;
}
