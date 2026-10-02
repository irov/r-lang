module regression.float_invalid_compound;

i32 main() {
    f64 value = 5.0;
    value %= 2.0;
    return 0;
}
