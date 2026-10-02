module test.codegen.bounds;

i32 main() {
    i32[2] values = {1, 2};
    const i32[] view = &values;
    i32 value = view[2];
    return value;
}
