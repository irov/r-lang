module regression.numeric_implicit_sign_change;

i32 main() {
    i32 signed_value = 1;
    u64 unsigned_value = signed_value;
    unsigned_value as void;
    return 0;
}
