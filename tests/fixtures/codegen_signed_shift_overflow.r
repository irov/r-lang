module test.codegen.signed_shift_overflow;

i32 main() {
    i32 value = 1;
    usize count = 31;
    i32 shifted = value << count;
    return shifted;
}
