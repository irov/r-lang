module codegen.core_assume;

i32 main() {
    bool condition = true;
    unsafe {
        core::assume(condition);
    }
    return 0;
}
