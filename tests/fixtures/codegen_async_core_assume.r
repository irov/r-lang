module codegen.async_core_assume;

async i32 main() {
    bool condition = true;
    unsafe {
        core::assume(condition);
    }
    return 0;
}
