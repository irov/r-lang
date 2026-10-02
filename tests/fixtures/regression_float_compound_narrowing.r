module regression.float_compound_narrowing;

i32 main() {
    f32 destination = 1.0f32;
    f64 source = 2.0;
    destination += source;
    return 0;
}
