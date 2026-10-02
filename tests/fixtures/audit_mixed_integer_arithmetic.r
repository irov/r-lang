module audit.mixed_integer_arithmetic;

i32 main() {
    u32 narrow = 1;
    u64 wide = 2;
    u64 value = narrow + wide;
    i32 selected = value == 3 ? 0 : 1;
    return selected;
}
