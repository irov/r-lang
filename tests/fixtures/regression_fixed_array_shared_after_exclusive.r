module regression.fixed_array_shared_after_exclusive;

i32 main() {
    i32[1] values = {17};
    i32[] exclusive = &values;
    const i32[] shared = &values;
    i32 first = exclusive[0];
    return first + shared[0] - 34;
}
