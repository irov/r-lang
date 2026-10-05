module test.codegen.unwind_second_panic_finally;

i32 main() {
    i32 maximum = 2147483647;
    i32 one = 1;
    try {
        maximum += one;
    } finally {
        i32 zero = 0;
        i32 unreachable = one / zero;
        unreachable as void;
    }
    return 0;
}
