module audit.float_to_integer_cast;

i32 main() {
    f64 source = 1.75;
    i32 value = source as i32;
    i32 selected = value == 1 ? 0 : 1;
    return selected;
}
