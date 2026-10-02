module regression.numeric_implicit_narrowing;

i32 main() {
    i64 wide = 1;
    i32 narrow = wide;
    return narrow;
}
