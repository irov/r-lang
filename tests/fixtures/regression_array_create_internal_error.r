module regression.array_create_internal_error;

i32 main() {
    array<i32> values = std.array::create::<i32>();
    drop values;
    return 0;
}
