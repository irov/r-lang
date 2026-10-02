module test.codegen.main_typed_duration;

i32 main() {
    throw std.time::duration_error::invalid_nanoseconds;
}
