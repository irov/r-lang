module codegen.multiplication_overflow;

i32 main() {
    i32 value = 1073741824;
    i32 two = 2;
    i32 result = value * two;
    return result;
}
