module regression.generic_borrowed_storage_return;

@generic<T>
struct Box {
    T value;
};

@generic<T>
const T* get(const Box<T>* box) {
    return &box->value;
}

@generic<T>
const T* first(const T[] values) {
    return &values[0];
}

i32 main() {
    Box<i32> box = Box<i32> { .value = 7 };
    i32[1] values = {11};
    const i32[] view = &values;
    const i32* field = get(&box);
    const i32* element = first(view);
    return *field + *element - 18;
}
