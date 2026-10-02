module test.codegen.generic_borrow;
@generic<T>
T identity(T value) { return move value; }
const i32* borrowed(const i32* value) {
    const i32* result = identity(value);
    return result;
}
i32 main() {
    i32 value = 23;
    const i32* pointer = &value;
    const i32* result = borrowed(pointer);
    if (*result != 23) { return 1; }
    if (*pointer != 23 || value != 23) { return 2; }
    return 0;
}
