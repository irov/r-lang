module test.codegen.c_abi_compound_overflow;

i32 main() {
    c_short value = 32767i32 as c_short;
    c_int one = 1i32 as c_int;
    value += one;
    return value as i32;
}
