module audit.float_arithmetic;

i32 main() {
    f64 left = 1.5;
    f64 right = 2.25;
    f64 value = left + right;
    i32 selected = value == 3.75 ? 0 : 1;
    return selected;
}
