module test.codegen.i8_compound_overflow;

i32 main() {
    i8 maximum = 127;
    maximum += 1;
    return 0;
}
