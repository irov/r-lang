module test.codegen.invalid_shift;

i32 main() {
    u32 value = 1;
    usize count = 32;
    u32 shifted = value << count;
    return shifted as i32;
}
