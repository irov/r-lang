module test.codegen.c_layouts_bit_range;

/* R-FFI-0062, R-EXPR-0016 (B9.3): a value outside the range of a bit-field panics with
   invalid_conversion where it is stored; values at both ends of the range are stored. */

@repr(C)
struct Bits {
    c_uint a : 3;
    c_int c : 7;
};

Bits store(Bits value, c_int c) {
    Bits result = value;
    result.c = c;
    return result;
}

i32 main() {
    Bits bits = Bits { .a = 7u32 as c_uint, .c = -64i32 as c_int };
    Bits high = store(bits, 63i32 as c_int);
    if (high.c != 63i32 as c_int || high.a != 7u32 as c_uint || bits.c != -64i32 as c_int) {
        return 1;
    }
    Bits outside = store(bits, 64i32 as c_int);
    return outside.c as i32;
}
