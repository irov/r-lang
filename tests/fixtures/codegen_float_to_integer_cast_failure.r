module test.codegen.float_to_integer_cast_failure;

i32 main() {
    f64 source = -1.0;
    u32 value = source as u32;
    return value as i32;
}
