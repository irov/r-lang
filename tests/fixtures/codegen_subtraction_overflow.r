module codegen.subtraction_overflow;

i32 main() {
    i32 minimum = -2147483648;
    i32 one = 1;
    i32 result = minimum - one;
    return result;
}
