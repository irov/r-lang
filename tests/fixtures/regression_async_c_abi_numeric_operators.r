module regression.async_c_abi_numeric_operators;

async i32 main() {
    c_short left = 3i32 as c_short;
    c_int right = 4i32 as c_int;
    c_int sum = left + right;
    left += right;
    left++;
    c_int negated = -right;
    c_int shifted = left << 1usize;
    c_double floating = 1.5f64 as c_double;
    c_double mixed = floating + right;

    if (sum != 7i32 as c_int) { return 1; }
    if (left != 8i32 as c_short) { return 2; }
    if (negated != -4i32 as c_int) { return 3; }
    if (shifted != 16i32 as c_int) { return 4; }
    i32 selected = mixed == 5.5f64 as c_double ? 0 : 5;
    return selected;
}
