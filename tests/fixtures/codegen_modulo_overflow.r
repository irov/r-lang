module codegen.modulo_overflow;

i32 main() {
    i32 minimum = -2147483648;
    i32 negative_one = -1;
    i32 result = minimum % negative_one;
    return result;
}
