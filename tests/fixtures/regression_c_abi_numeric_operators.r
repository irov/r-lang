module regression.c_abi_numeric_operators;

protected c_int update(c_int left, c_int right) {
    left += right;
    left++;
    return left + right;
}

protected c_int promote(c_short left, c_ushort right) {
    return left + right;
}

protected c_long mixed_rank(c_uint left, c_long right) {
    return left + right;
}

protected c_ulong unsigned_common(c_long left, c_ulong right) {
    return left + right;
}

protected c_double add_mixed(c_int left, c_double right) {
    return left + right;
}

protected c_int unary_and_shift(c_short value, usize count) {
    c_int promoted = +value;
    c_int negated = -promoted;
    c_int shifted = value << count;
    return shifted - negated;
}

protected c_int bitwise(c_int left, c_int right) {
    return (left | right) ^ right;
}

i32 main() {
    c_int one = 1i32 as c_int;
    c_int two = 2i32 as c_int;
    if (update(one, two) != 6i32 as c_int) { return 1; }

    c_short three = 3i32 as c_short;
    c_ushort four = 4i32 as c_ushort;
    if (promote(three, four) != 7i32 as c_int) { return 2; }

    c_uint five = 5i32 as c_uint;
    c_long six = 6i32 as c_long;
    if (mixed_rank(five, six) != 11i32 as c_long) { return 3; }

    c_ulong seven = 7i32 as c_ulong;
    if (unsigned_common(six, seven) != 13i32 as c_ulong) { return 4; }

    c_double half = 0.5f64 as c_double;
    if (add_mixed(two, half) != 2.5f64 as c_double) { return 5; }

    c_short narrow = -32768i32 as c_short;
    narrow += one;
    if (narrow != -32767i32 as c_short) { return 6; }

    c_ushort wrapped = 65535i32 as c_ushort;
    wrapped++;
    if (wrapped != 0i32 as c_ushort) { return 7; }

    if (unary_and_shift(three, 1usize) != 9i32 as c_int) { return 8; }
    if (bitwise(one, two) != one) { return 9; }
    return 0;
}
