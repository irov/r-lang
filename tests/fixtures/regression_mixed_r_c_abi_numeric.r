module regression.mixed_r_c_abi_numeric;

i32 main() {
    c_int c_value = 1 as c_int;
    i32 r_value = 2;
    i32 result = c_value + r_value;
    return result;
}
