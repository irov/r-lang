module test.value_types.probe;

error value_error {
    i32 code;
};

i32 read_value(const i32[] values, u32 index) throws value_error {
    return values[index];
}

i32 compute() throws value_error {
    i32[3] values = { 1, 2, 3 };
    const i32[] view = &values;
    i32 value = read_value(view, 1);
    return value;
}

i32 main() {
    o<i32> maybe = o::some(4);
    try {
        i32 value = compute();
        return value - 2;
    } catch (value_error error) {
        return error.code;
    }
}
