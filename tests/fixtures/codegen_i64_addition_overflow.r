module test.codegen.i64_addition_overflow;

i32 main() {
    i64 maximum = 9223372036854775807;
    i64 overflow = maximum + 1;
    return 0;
}
