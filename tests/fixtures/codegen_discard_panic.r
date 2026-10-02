module codegen.discard_panic;

i32 main() {
    i32 maximum = 2147483647;
    i32 one = 1;
    (maximum + one) as void;
    return 0;
}
