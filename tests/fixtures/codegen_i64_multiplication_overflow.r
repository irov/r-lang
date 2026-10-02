module test.codegen.i64_multiplication_overflow;

i32 main() {
    i64 operand = 3037000500;
    i64 overflow = operand * operand;
    return 0;
}
