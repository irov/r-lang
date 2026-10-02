module test.codegen.integer_cast_failure;

i32 main() {
    u16 source = 256;
    u8 narrowed = source as u8;
    return narrowed as i32;
}
