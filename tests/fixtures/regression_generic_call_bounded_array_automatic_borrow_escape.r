module regression.generic_call_bounded_array_automatic_borrow_escape;

const u8[] identity(const u8[] source) {
    return source;
}

@generic<T>
error Escape {
    const u8[] data;
    T tag;
};

@generic<T>
void leak(T tag) throws Escape<T> {
    u8[1] data = { 7 };
    const u8[] escaped = identity(data);
    throw Escape<T> { .data = escaped, .tag = move tag };
}

void instantiate() throws Escape<i32> {
    leak(1);
}

i32 main() {
    return 0;
}
