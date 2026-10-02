module test.codegen.isize_division_overflow;

i32 main() {
    isize minimum = -9223372036854775808;
    isize overflow = minimum / -1;
    return 0;
}
