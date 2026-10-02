module test.audit.alloc_new_error_parameter_control;

i32 inspect(std.alloc::new_error<own i32*> failure) {
    return *(failure.value);
}

i32 main() {
    return 0;
}
